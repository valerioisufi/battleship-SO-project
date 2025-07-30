
#define GRID_SIZE 10
#define NUM_SHIPS 5

typedef struct {
    char grid[GRID_SIZE][GRID_SIZE]; // '.' = vuoto, 'O' = nave, 'X' = colpito, '*' = mancato
    int ships_left;
} GameBoard;

typedef struct {
    unsigned int user_id; // ID dell'utente
    char *username; // Nome utente (max 30 caratteri + terminatore), NULL se non impostato
    GameBoard board; // La griglia di gioco dell'utente
} PlayerState;

typedef struct {
    unsigned int game_id; // ID della partita
    char *game_name; // Nome della partita (max 30 caratteri + terminatore), NULL se non impostato
    PlayerState *players; // Array di giocatori nella partita
    int players_count; // Numero attuale di giocatori nella partita
    int players_capacity; // Capacità attuale dell'array dei giocatori
} GameState;

GameState *create_game_state(unsigned int game_id, const char *game_name);
int add_player_to_game(GameState *game, int player_id, char *username);
int remove_player_from_game(GameState *game, unsigned int player_id);

int init_board(GameBoard *board);
int place_ship(GameBoard *board, int x, int y);
int attack(GameBoard *board, int x, int y);