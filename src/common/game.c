#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <pthread.h>

#include "game.h"
#include "utils/debug.h"

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
int add_player_to_game(GameState *game, int player_id, char *username) {
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
    game->players[game->players_count].user_id = player_id;
    game->players[game->players_count].username = strdup(username);
    if (!game->players[game->players_count].username) {
        fprintf(stderr, "Errore durante l'allocazione della memoria per il nome utente.\n");
        return -1;
    }
    game->players_count++;
    return 0;
}

/**
 * Rimuove un giocatore da una partita.
 * Sposta l'ultimo giocatore nella posizione corrente e riduce il conteggio dei giocatori.
 * @param game Puntatore alla struttura GameState della partita.
 * @param player_id ID del giocatore da rimuovere.
 * @return 0 se il giocatore è stato rimosso con successo, -1 se il giocatore non è stato trovato.
 */
int remove_player_from_game(GameState *game, unsigned int player_id) {
    if(game == NULL || game->players == NULL) {
        fprintf(stderr, "remove_player_from_game: game or players array is NULL\n");
        return -1;
    }

    for (unsigned int i = 0; i < game->players_count; i++) {
        if (game->players[i].user_id == player_id) {
            // Sposta l'ultimo giocatore nella posizione corrente
            game->players[i] = game->players[game->players_count - 1];
            game->players_count--;
            return 0; // Giocatore rimosso con successo
        }
    }
    
    return -1; // Giocatore non trovato
}

int init_board(GameBoard *board) {
    if (board == NULL) {
        fprintf(stderr, "init_board: board is NULL\n");
        return -1;
    }

    memset(board->grid, '.', sizeof(board->grid)); // Inizializza la griglia a vuoto
    board->ships_left = NUM_SHIPS; // Imposta il numero di navi rimaste
    return 0;
}

int place_ship(GameBoard *board, int x, int y) {
    if (board == NULL || x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        fprintf(stderr, "place_ship: invalid board or coordinates\n");
        return -1;
    }

    if (board->grid[x][y] == 'O') {
        fprintf(stderr, "place_ship: ship already placed at (%d, %d)\n", x, y);
        return -1; // Nave già presente
    }

    // Controlla celle adiacenti (inclusi diagonali)
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            int nx = x + dx;
            int ny = y + dy;
            if (nx >= 0 && nx < GRID_SIZE && ny >= 0 && ny < GRID_SIZE) {
                if (board->grid[nx][ny] == 'O') {
                    fprintf(stderr, "place_ship: adjacent ship at (%d, %d)\n", nx, ny);
                    return -1;
                }
            }
        }
    }

    board->grid[x][y] = 'O'; // Posiziona la nave
    return 0;
}

int attack(GameBoard *board, int x, int y) {
    if (board == NULL || x < 0 || x >= GRID_SIZE || y < 0 || y >= GRID_SIZE) {
        fprintf(stderr, "attack: invalid board or coordinates\n");
        return -1;
    }

    if (board->grid[x][y] == 'X' || board->grid[x][y] == '*') {
        fprintf(stderr, "attack: already attacked at (%d, %d)\n", x, y);
        return -1; // Già attaccato
    }

    if (board->grid[x][y] == 'O') {
        board->grid[x][y] = 'X'; // Colpito
        board->ships_left--;
        return 1; // Colpito
    } else {
        board->grid[x][y] = '*'; // Mancato
        return 0; // Mancato
    }
}