#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "game.h"
#include "utils/debug.h"

FleetRequirement fleet_requirement = {1,1,2,1,0}; // Requisiti di flotta per la partita

/**
 * Crea e inizializza lo stato di una partita.
 * @param game_id ID della partita.
 * @param game_name Nome della partita.
 * @return Puntatore a GameState se la creazione è riuscita, NULL altrimenti.
 */
GameState *create_game_state(unsigned int game_id, const char *game_name) {
    GameState *game = (GameState *)malloc(sizeof(GameState));
    if (!game) {
        LOG_ERROR("Memory allocation for GameState failed");
        return NULL;
    }

    game->game_id = game_id;
    game->game_name = (game_name) ? strdup(game_name) : NULL;
    if (game->game_name == NULL && game_name != NULL) {
        LOG_ERROR("Memory allocation for game_name failed");
        free(game);
        return NULL;
    }

    game->players_count = 0;
    game->players_capacity = 4; // Initial capacity

    game->players = (PlayerState *)malloc(game->players_capacity * sizeof(PlayerState));
    if (!game->players) {
        free(game->game_name);
        free(game);
        return NULL;
    }

    return game;
}

/**
 * Aggiunge un giocatore a una partita.
 * Se l'array dei giocatori è pieno, raddoppia la sua capacità.
 * @param game_id ID della partita a cui aggiungere il giocatore.
 * @param player_id ID del giocatore da aggiungere.
 * @return 0 se il giocatore è stato aggiunto con successo, -1 in caso di errore.
 */
int add_player_to_game_state(GameState *game, int player_id, char *username) {
    if(game == NULL || game->players == NULL) {
        fprintf(stderr, "add_player_to_game: game or players array is NULL\n");
        return -1;
    }

    if(username == NULL) {
        fprintf(stderr, "add_player_to_game: username is NULL\n");
        return -1;
    }

    // Se l'array dei giocatori è pieno, raddoppia la sua capacità
    if (game->players_count >= game->players_capacity) {
        size_t new_capacity = game->players_capacity * 2;
        PlayerState *new_players = (PlayerState *)realloc(game->players, new_capacity * sizeof(PlayerState));
        if (new_players) {
            game->players = new_players;
            game->players_capacity = new_capacity;
        } else {
            // realloc fallito, non si può aggiungere il giocatore
            return -1;
        }
    }
    
    // Aggiungi il giocatore
    game->players[game->players_count].user.user_id = player_id;
    game->players[game->players_count].user.username = strdup(username);
    if (!game->players[game->players_count].user.username) {
        fprintf(stderr, "Errore durante l'allocazione della memoria per il nome utente.\n");
        return -1;
    }
    game->players[game->players_count].fleet = NULL;
    game->players_count++;

    init_board(&game->players[game->players_count - 1].board); // Inizializza la griglia di gioco del nuovo giocatore
    return 0;
}

/**
 * Rimuove un giocatore da una partita.
 * Sposta l'ultimo giocatore nella posizione corrente e riduce il conteggio dei giocatori.
 * @param game Puntatore alla struttura GameState della partita.
 * @param player_id ID del giocatore da rimuovere.
 * @return 0 se il giocatore è stato rimosso con successo, -1 se il giocatore non è stato trovato.
 */
int remove_player_from_game_state(GameState *game, unsigned int player_id) {
    // TODO dovrei evitare di rimuovere il giocatore per permettere la riconnessione e per mantenere l'ordine di inserimento
    if(game == NULL || game->players == NULL) {
        fprintf(stderr, "remove_player_from_game: game or players array is NULL\n");
        return -1;
    }

    for (unsigned int i = 0; i < game->players_count; i++) {
        if (game->players[i].user.user_id == player_id) {
            // Sposta l'ultimo giocatore nella posizione corrente
            game->players[i] = game->players[game->players_count - 1];
            game->players_count--;
            return 0; // Giocatore rimosso con successo
        }
    }
    
    return -1; // Giocatore non trovato
}

/**
 * Ottiene lo stato di un giocatore della partita.
 * @param game Puntatore alla struttura GameState della partita.
 * @param player_id ID del giocatore di cui ottenere lo stato.
 * @return Puntatore a PlayerState se il giocatore è trovato, NULL altrimenti.
 */
PlayerState *get_player_state(GameState *game, unsigned int player_id) {
    if (game == NULL || game->players == NULL) {
        fprintf(stderr, "get_player_state: game or players array is NULL\n");
        return NULL;
    }

    for (unsigned int i = 0; i < game->players_count; i++) {
        if (game->players[i].user.user_id == player_id) {
            return &game->players[i];
        }
    }

    return NULL; // Giocatore non trovato
}

void free_game_state(GameState *game) {
    if (game == NULL) return;

    for (unsigned int i = 0; i < game->players_count; i++) {
        free(game->players[i].user.username);
        free(game->players[i].fleet); // Libera la flotta se allocata
    }
    free(game->players);
    free(game->game_name);
    free(game);
}

/**
 * Inizializza la griglia di gioco di un giocatore.
 * @param board Puntatore alla struttura GameBoard da inizializzare.
 * @return 0 se l'inizializzazione è riuscita, -1 in caso di errore.
 */
int init_board(GameBoard *board) {
    if (board == NULL) {
        return -1;
    }

    memset(board->grid, '.', sizeof(board->grid)); // Inizializza la griglia a vuoto
    board->ships_left = 0; // Imposta il numero di navi rimaste
    return 0;
}

 /**
 * Imposta il valore di una cella nella griglia di gioco.
 * @param board Puntatore alla struttura GameBoard su cui impostare la cella.
 * @param x Coordinata X della cella da impostare.
 * @param y Coordinata Y della cella da impostare.
 * @param value Valore da impostare nella cella.
 * @return 0 se l'operazione è riuscita, -1 in caso di errore.
 */
int set_cell(GameBoard *board, int x, int y, char value) {
    if (board == NULL || x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        return -1;
    }

    board->grid[x][y] = value; // Imposta il valore della cella
    return 0;
}

int is_ship_present(GameBoard *board, int x, int y) {
    if (board == NULL || x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        return -1;
    }
    return (board->grid[x][y] >= 'A' && board->grid[x][y] <= 'E'); // Controlla se c'è una nave nella cella
}

int can_place_ship(GameBoard *board, ShipPlacement *ship) {
    if (board == NULL || ship == NULL) {
        return -1; // Parametri non validi
    }

    int x = ship->x;
    int y = ship->y;
    int dim = ship->dim;
    int vertical = ship->vertical;

    if (x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE || dim <= 0) {
        return -1; // Parametri non validi
    }

    if (vertical) {
        if (y + dim > GRID_SIZE) return -1; // Fuori dai limiti verticali
        for (int i = 0; i < dim; i++) {
            if (board->grid[x][y + i] != '.') return -1; // Cella già occupata
        }
    } else {
        if (x + dim > GRID_SIZE) return -1; // Fuori dai limiti orizzontali
        for (int i = 0; i < dim; i++) {
            if (board->grid[x + i][y] != '.') return -1; // Cella già occupata
        }
    }
    
    return 0; // Posizione valida per piazzare la nave
}
/**
 * Posiziona una nave sulla griglia di gioco.
 * @param board Puntatore alla struttura GameBoard su cui posizionare la nave.
 * @param x Coordinata X della cella in cui posizionare la nave.
 * @param y Coordinata Y della cella in cui posizionare la nave.
 * @return 0 se la nave è stata posizionata con successo, -1 in caso di errore.
 */
int place_ship(GameBoard *board, ShipPlacement *ship) {
    if (board == NULL || ship == NULL) {
        return -1;
    }
    if (board->ships_left >= NUM_SHIPS) {
        return -1; // Non è possibile posizionare più navi
    }

    if (can_place_ship(board, ship) != 0) {
        return -1; // Posizione non valida per piazzare la nave
    }

    if(ship->vertical) {
        for (int i = 0; i < ship->dim; i++) {
            board->grid[ship->x][ship->y + i] = 'A' + ship->dim - 1; // Posiziona la nave
        }
    } else {
        for (int i = 0; i < ship->dim; i++) {
            board->grid[ship->x + i][ship->y] = 'A' + ship->dim - 1; // Posiziona la nave
        }
    }

    board->ships_left++;
    return 0;
}

/**
 * Esegue un attacco su una cella specificata della griglia di gioco.
 * @param player_state Puntatore allo stato del giocatore che esegue l'attacco.
 * @param x Coordinata X della cella da attaccare.
 * @param y Coordinata Y della cella da attaccare.
 * @return 0 se l'attacco ha mancato, 1 se ha colpito una nave, 2 se ha affondato una nave, -1 in caso di errore, -2 se la cella è già stata colpita.
 */
int attack(PlayerState *player_state, int x, int y) {
    if (player_state == NULL || x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        return -1; // Parametri non validi
    }
    if(player_state->fleet == NULL) {
        return -1; // Flotta non inizializzata
    }

    GameBoard *board = &player_state->board;
    char cell = board->grid[x][y];
    if (cell >= 'A' && cell <= 'E') {
        board->grid[x][y] = 'X'; // Colpito
        for (int i = 0; i < NUM_SHIPS; i++) {
            ShipPlacement *ship = &player_state->fleet->ships[i];

            if (ship->vertical) {
                if (ship->x == x && ship->y <= y && ship->y + ship->dim > y){
                    int hit = 0;
                    for (int j = 0; j < ship->dim; j++) {
                        if (board->grid[ship->x][ship->y + j] == 'X') {
                            hit = 1;
                        }
                    }
                    if(hit == ship->dim){
                        board->ships_left--; // Se tutte le celle della nave sono colpite, decrementa il numero di navi rimaste
                        return 2; // Colpito con successo e nave affondata
                    }
                    break;
                }
            } else {
                if (ship->y == y && ship->x <= x && ship->x + ship->dim > x){
                    int hit = 0;
                    for (int j = 0; j < ship->dim; j++) {
                        if (board->grid[ship->x + j][ship->y] == 'X') {
                            hit = 1;
                        }
                    }
                    if(hit == ship->dim){
                        board->ships_left--; // Se tutte le celle della nave sono colpite, decrementa il numero di navi rimaste
                        return 2; // Colpito con successo e nave affondata
                    }
                    break;
                }
            }

        }
        board->ships_left--; // Decrementa il numero di navi rimaste
        return 1; // Colpito con successo
    } else if (cell == '.') {
        board->grid[x][y] = '*'; // Mancato
        return 0; // Mancato
    } else {
        return -2; // Già colpito o mancato
    }
}