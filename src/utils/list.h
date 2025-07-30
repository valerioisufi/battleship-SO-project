#include <unistd.h>
#include <pthread.h>

typedef struct {
    pthread_mutex_t mutex; // Mutex per la protezione del dato in `ptr`
    ssize_t next_free_index; // Indice del prossimo nodo libero (-1 se il nodo corrente è occupato)
    size_t index; // Indice univoco di questo nodo
    void *ptr; // Puntatore ai dati specifici
} ListItem;

typedef struct {
    ListItem **pages; // Pagine di elementi (array di puntatori a ListItem)
    size_t first_index_free; // Indice del primo elemento libero
    pthread_mutex_t mutex; // Mutex per proteggere `first_index_free` e l'allocazione delle pagine
} ListManager;


ListManager *create_list_manager();
ListItem *get_node(size_t index, ListManager *manager);
ListItem *add_node(ListManager *manager, void *ptr);
void release_node(ListManager *manager, size_t index);