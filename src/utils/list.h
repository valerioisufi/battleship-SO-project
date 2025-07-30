#include <unistd.h>
#include <pthread.h>

#define PAGE_SIZE_BITS 8 // 2^8 = 256 elementi per pagina
#define PAGE_INDEX_BITS 10 // 2^10 = 1024 pagine

#define PAGE_SIZE (1 << PAGE_SIZE_BITS)
#define MAX_PAGES (1 << PAGE_INDEX_BITS)
#define MAX_ELEMENTS (MAX_PAGES * PAGE_SIZE)

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
void free_list_manager(ListManager *manager);

ListItem *get_node(size_t index, ListManager *manager);
ListItem *add_node(ListManager *manager, void *ptr);
void release_node(ListManager *manager, size_t index);