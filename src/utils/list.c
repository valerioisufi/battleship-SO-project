#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "utils/list.h"
#include "utils/debug.h"

/**
 * Crea e inizializza un nuovo gestore di lista.
 */
ListManager *create_list_manager() {
    ListManager *manager = (ListManager *)calloc(1, sizeof(ListManager));
    if (!manager) {
        LOG_ERROR("calloc per ListManager non riuscito");
        exit(EXIT_FAILURE);
    }

    manager->pages = (ListItem **)calloc(MAX_PAGES, sizeof(ListItem *));
    if (!manager->pages) {
        LOG_ERROR("calloc per manager->pages non riuscito");
        free(manager);
        exit(EXIT_FAILURE);
    }

    manager->first_index_free = 0;
    pthread_mutex_init(&manager->mutex, NULL);
    return manager;
}

/**
 * Libera tutta la memoria allocata dal ListManager, incluse tutte le pagine e i mutex associati.
 * Dopo la chiamata, il puntatore manager non sarà più valido.
 * @param manager Puntatore al gestore della lista da liberare.
 */
void free_list_manager(ListManager *manager) {
    if (!manager) return;
    for (size_t i = 0; i < MAX_PAGES; i++) {
        if (manager->pages[i]) {
            // Distruggi tutti i mutex nei nodi della pagina
            for(size_t j = 0; j < PAGE_SIZE; j++) {
                pthread_mutex_destroy(&manager->pages[i][j].mutex);
                if(manager->pages[i][j].ptr) {
                    free(manager->pages[i][j].ptr); // Libera i dati puntati
                }
            }
            free(manager->pages[i]);
        }
    }
    free(manager->pages);
    pthread_mutex_destroy(&manager->mutex);
    free(manager);
}

/**
 * Ottiene un puntatore a un ListItem dato il suo index.
 * Alloca una nuova pagina se necessario. NON è thread-safe da solo.
 */
ListItem *get_node_nolock(size_t index, ListManager *manager) {
    if (index >= MAX_ELEMENTS) {
        LOG_ERROR("Error: index %zu is out of bounds.\n", index);
        exit(EXIT_FAILURE);
    }

    size_t page_index = index >> PAGE_SIZE_BITS;

    // Se la pagina non esiste, la allochiamo
    if (manager->pages[page_index] == NULL) {
        ListItem *page = (ListItem *)calloc(PAGE_SIZE, sizeof(ListItem));
        if (!page) {
            LOG_ERROR("calloc per nuova pagina non riuscito");
            exit(EXIT_FAILURE);
        }

        // Inizializza i nuovi nodi
        for (size_t i = 0; i < PAGE_SIZE; i++) {
            page[i].ptr = NULL;
            pthread_mutex_init(&page[i].mutex, NULL);

            size_t current_index = (page_index * PAGE_SIZE) + i;
            page[i].index = current_index;

            page[i].next_free_index = current_index + 1;
        }

        manager->pages[page_index] = page;
    }

    size_t node_in_page_offset = index & (PAGE_SIZE - 1);
    return &manager->pages[page_index][node_in_page_offset];
}

/**
 * Ottiene un puntatore a un ListItem (versione pubblica e thread-safe).
 * Gestisce i lock per garantire l'accesso sicuro alla tabella delle pagine.
 */
ListItem *get_node(size_t index, ListManager *manager) {
    size_t page_index = index >> PAGE_SIZE_BITS;

    // Se la pagina non esiste, dobbiamo acquisire il lock per crearla.
    if (manager->pages[page_index] == NULL) {
        pthread_mutex_lock(&manager->mutex);
        // Chiamiamo la versione _nolock dopo aver acquisito il lock.
        if (manager->pages[page_index] == NULL) { // double-checked locking
            get_node_nolock(index, manager);
        }
        pthread_mutex_unlock(&manager->mutex);
    }
    
    // Una volta che la pagina esiste, possiamo accedere direttamente senza lock.
    size_t node_in_page_offset = index & (PAGE_SIZE - 1);
    return &manager->pages[page_index][node_in_page_offset];
}


/**
 * Aggiunge un puntatore alla lista, restituendo il nodo che lo contiene.
 * Usa il mutex della lista per garantire l'accesso sicuro alla free-list.
 */
ListItem *add_node(ListManager *manager, void *ptr) {
    pthread_mutex_lock(&manager->mutex);
    
    size_t node_index = manager->first_index_free;
    ListItem *node = get_node_nolock(node_index, manager);
    
    // Aggiorna la testa della free-list
    manager->first_index_free = node->next_free_index;

    pthread_mutex_unlock(&manager->mutex);

    // Popola il nodo con i dati
    // Il lock sul singolo nodo non è necessario qui,
    // perché nessun altro può ottenerlo dalla free-list
    node->next_free_index = -1; // Marca come "occupato"
    node->ptr = ptr;
    
    return node;
}

/**
 * Rende un nodo nuovamente disponibile nella free-list.
 * Usa il mutex della lista per garantire l'accesso sicuro alla free-list.
 * 
 * Questa funzione NON libera la memoria puntata da `node->ptr`.
 */
void release_node(ListManager *manager, size_t index) {
    pthread_mutex_lock(&manager->mutex);

    ListItem *node = get_node_nolock(index, manager);

    // Se è già libero, non fare nulla
    if (node->next_free_index != -1) {
        pthread_mutex_unlock(&manager->mutex);
        return;
    }

    // Inserisci il nodo in testa alla free-list
    node->next_free_index = manager->first_index_free;
    manager->first_index_free = index;

    pthread_mutex_unlock(&manager->mutex);
}