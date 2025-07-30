#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#include <pthread.h>

#include "users.h"
#include "utils/debug.h"
#include "utils/list.h"

ListManager *users_list = NULL;
ListManager *games_list = NULL;

/**
 * Inizializza le liste degli utenti e delle partite.
 * Se le liste sono già state inizializzate, non fa nulla.
 */
void init_lists() {
    if (users_list == NULL) {
        users_list = create_list_manager();
    }
    if (games_list == NULL) {
        games_list = create_list_manager();
    }
}

/**
 * Crea un nuovo utente e lo aggiunge alla lista degli utenti.
 * @param username Nome dell'utente da creare.
 * @param socket_fd File descriptor della socket associata all'utente.
 * @return ID dell'utente creato, o -1 in caso di errore.
 */
int create_user(const char *username, int socket_fd) {
    User *new_user = (User *)malloc(sizeof(User));
    if (!new_user) return -1;

    if (username) {
        new_user->username = strdup(username);
        if (!new_user->username) {
            free(new_user);
            return -1;
        }
    } else {
        new_user->username = NULL;
    }
    
    new_user->socket_fd = socket_fd;
    new_user->game_id = 0;

    ListItem *node = add_node(users_list, new_user);
    new_user->id = node->index;
    
    return new_user->id;
}

/**
 * Rimuove un utente dalla lista degli utenti e libera le risorse associate.
 * @param user_id ID dell'utente da rimuovere.
 */
void remove_user(unsigned int user_id) {
    ListItem *node = get_node(user_id, users_list);
    
    // Blocca il nodo per garantire che nessuno legga i dati mentre li liberiamo
    pthread_mutex_lock(&node->mutex);
    
    User *user_data = (User *)node->ptr;
    if (user_data) {
        free(user_data->username);
        free(user_data);
        node->ptr = NULL;
    }
    
    pthread_mutex_unlock(&node->mutex);
    
    // Ora che i dati sono stati liberati, rimetti il nodo nella free list
    release_node(users_list, user_id);
}

/**
 * Aggiorna il file descriptor della socket associato a un utente.
 * @param user_id ID dell'utente da aggiornare.
 * @param socket_fd Nuovo file descriptor della socket.
 * @return 0 se l'aggiornamento è andato a buon fine, -1 in caso di errore.
 */
int update_user_socket_fd(unsigned int user_id, int socket_fd) {
    ListItem *node = get_node(user_id, users_list);
    int success = -1;

    pthread_mutex_lock(&node->mutex);
    
    if (node->ptr) {
        User *user = (User *)node->ptr;
        user->socket_fd = socket_fd;
        success = 0;
    }

    pthread_mutex_unlock(&node->mutex);
    return success;
}

/**
 * Ottiene il file descriptor della socket associata a un utente.
 * @param user_id ID dell'utente di cui ottenere il file descriptor.
 * @return File descriptor della socket dell'utente, o -1 se l'utente non esiste.
 */
int get_user_socket_fd(unsigned int user_id) {
    ListItem *node = get_node(user_id, users_list);
    int socket_fd = -1;

    pthread_mutex_lock(&node->mutex);
    
    if (node->ptr) {
        User *user = (User *)node->ptr;
        socket_fd = user->socket_fd;
    }

    pthread_mutex_unlock(&node->mutex);
    return socket_fd;
}

/**
 * Aggiorna il nome utente associato a un ID utente.
 * @param user_id ID dell'utente da aggiornare.
 * @param new_username Nuovo nome utente da assegnare.
 * @return 0 se l'aggiornamento è andato a buon fine, -1 in caso di errore.
 */
int update_user_username(unsigned int user_id, const char *new_username) {
    ListItem *node = get_node(user_id, users_list);
    int success = -1;

    pthread_mutex_lock(&node->mutex);
    
    if (node->ptr) {
        User *user = (User *)node->ptr;
        if(user->username) {
            free(user->username);
        }
        user->username = strdup(new_username);
        if (user->username) {
            success = 0;
        }
    }

    pthread_mutex_unlock(&node->mutex);
    return success;
}

/**
 * Ottiene il nome utente associato a un ID utente.
 * @param user_id ID dell'utente di cui ottenere il nome.
 * @return Nome dell'utente, o NULL se l'utente non esiste.
 */
char *get_username_by_id(unsigned int user_id) {
    ListItem *node = get_node(user_id, users_list);
    char *username = NULL;

    pthread_mutex_lock(&node->mutex);

    if (node->ptr) {
        User *user = (User *)node->ptr;
        username = strdup(user->username);
    }

    pthread_mutex_unlock(&node->mutex);
    return username;
}

/**
 * Aggiorna l'ID della partita associata a un utente.
 * @param user_id ID dell'utente da aggiornare.
 * @param game_id Nuovo ID della partita, 0 se l'utente non è in una partita.
 * @return 0 se l'aggiornamento è andato a buon fine, -1 in caso di errore.
 */
int update_user_game_id(unsigned int user_id, unsigned int game_id) {
    ListItem *node = get_node(user_id, users_list);
    int success = -1;

    pthread_mutex_lock(&node->mutex);
    
    if (node->ptr) {
        User *user = (User *)node->ptr;
        user->game_id = game_id;
        success = 0;
    }

    pthread_mutex_unlock(&node->mutex);
    return success;
}

/**
 * Ottiene l'ID della partita associata a un utente.
 * @param user_id ID dell'utente di cui ottenere l'ID della partita.
 * @return ID della partita associata all'utente, o 0 se l'utente non è in una partita.
 */
int get_user_game_id(unsigned int user_id) {
    ListItem *node = get_node(user_id, users_list);
    int game_id = 0;

    pthread_mutex_lock(&node->mutex);
    
    if (node->ptr) {
        User *user = (User *)node->ptr;
        game_id = user->game_id;
    }

    pthread_mutex_unlock(&node->mutex);
    return game_id;
}


/**
 * Crea una nuova partita e restituisce il suo ID.
 * @param game_name Nome della partita.
 * @param owner_id ID del giocatore che crea la partita.
 * @return ID della nuova partita, o -1 in caso di errore.
 */
int create_game(const char *game_name, unsigned int owner_id) {
    Game *new_game = (Game *)calloc(1, sizeof(Game));
    if (!new_game) return -1;
    
    new_game->game_name = strdup(game_name);
    if (!new_game->game_name) {
        free(new_game);
        return -1;
    }
    
    new_game->owner_id = owner_id;
    new_game->players_capacity = 8;
    new_game->players_count = 0;
    new_game->player_ids = (unsigned int *)malloc(new_game->players_capacity * sizeof(unsigned int));
    if (!new_game->player_ids) {
        free(new_game->game_name);
        free(new_game);
        return -1;
    }
    
    ListItem *node = add_node(games_list, new_game);
    new_game->game_id = node->index;
    
    // Aggiunge il creatore come primo giocatore
    add_player_to_game(new_game->game_id, owner_id);

    return new_game->game_id;
}

/**
 * Rimuove una partita e libera le risorse associate.
 * @param game_id ID della partita da rimuovere.
 */
void remove_game(unsigned int game_id) {
    ListItem *node = get_node(game_id, games_list);
    
    pthread_mutex_lock(&node->mutex);
    
    Game *game_data = (Game *)node->ptr;
    if (game_data) {
        free(game_data->game_name);
        free(game_data->player_ids);
        free(game_data);
        node->ptr = NULL;
    }
    
    pthread_mutex_unlock(&node->mutex);
    
    release_node(games_list, game_id);
}

/**
 * Aggiunge un giocatore a una partita.
 * Se l'array dei giocatori è pieno, raddoppia la sua capacità.
 * @param game_id ID della partita a cui aggiungere il giocatore.
 * @param player_id ID del giocatore da aggiungere.
 * @return 0 se il giocatore è stato aggiunto con successo, -1 in caso di errore.
 */
int add_player_to_game(unsigned int game_id, unsigned int player_id) {
    ListItem *node = get_node(game_id, games_list);
    int success = -1;

    pthread_mutex_lock(&node->mutex);
    
    Game *game = (Game *)node->ptr;
    if (game) {
        // Se l'array dei giocatori è pieno, raddoppia la sua capacità
        if (game->players_count >= game->players_capacity) {
            size_t new_capacity = game->players_capacity * 2;
            unsigned int *new_players = (unsigned int *)realloc(game->player_ids, new_capacity * sizeof(unsigned int));
            if (new_players) {
                game->player_ids = new_players;
                game->players_capacity = new_capacity;
            } else {
                // realloc fallito, non si può aggiungere il giocatore
                pthread_mutex_unlock(&node->mutex);
                return -1;
            }
        }
        
        // Aggiungi il giocatore
        game->player_ids[game->players_count] = player_id;
        game->players_count++;
        success = 0;
    }
    
    pthread_mutex_unlock(&node->mutex);
    return success;
}

int remove_player_from_game(unsigned int game_id, unsigned int player_id) {
    ListItem *node = get_node(game_id, games_list);
    int success = -1;

    pthread_mutex_lock(&node->mutex);
    
    Game *game = (Game *)node->ptr;
    if (game) {
        for (unsigned int i = 0; i < game->players_count; i++) {
            if (game->player_ids[i] == player_id) {
                // Sposta l'ultimo giocatore nella posizione corrente
                game->player_ids[i] = game->player_ids[game->players_count - 1];
                game->players_count--;
                success = 0;
                break;
            }
        }
    }
    
    pthread_mutex_unlock(&node->mutex);
    return success;
}