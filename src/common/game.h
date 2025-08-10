#ifndef GAME_H
#define GAME_H

#define GRID_SIZE 10

#define NUM_SHIPS 5
extern const int SHIP_PLACEMENT_SEQUENCE[NUM_SHIPS]; // Requisiti di flotta per la partita

typedef struct {
    unsigned int user_id; // ID dell'utente
    char *username; // Nome utente (max 30 caratteri + terminatore), NULL se non impostato
} UserInfo;

typedef struct {
    char grid[GRID_SIZE][GRID_SIZE]; // '.' = vuoto, 'O' = nave, 'X' = colpito, '*' = mancato
    int ships_left;
} GameBoard;

typedef struct {
    int dim5; // Numero di navi di dimensione 5
    int dim4; // Numero di navi di dimensione 4
    int dim3; // Numero di navi di dimensione 3
    int dim2; // Numero di navi di dimensione 2
    int dim1; // Numero di navi di dimensione 1
} FleetRequirement;

typedef struct {
    int x, y; // Coordinate della cella
    int dim; // Dimensione della nave
    int vertical; // 1 se la nave è verticale, 0 se orizzontale
} ShipPlacement;

typedef struct {
    ShipPlacement ships[NUM_SHIPS]; // Posizioni delle navi da piazzare
} FleetSetup;

typedef struct {
    int player_id;
    int x, y;
} AttackPosition;

typedef struct {
    UserInfo user; // Informazioni sull'utente
    GameBoard board; // La griglia di gioco dell'utente
    FleetSetup *fleet; // Posizioni delle navi piazzate
} PlayerState;

typedef struct {
    char *game_name; // Nome della partita (max 30 caratteri + terminatore), NULL se non impostato
    int game_id; // ID della partita
    
    PlayerState *players; // Array di giocatori nella partita
    unsigned int players_count; // Numero attuale di giocatori nella partita
    unsigned int players_capacity; // Capacità attuale dell'array dei giocatori
    
    int *player_turn_order; // Array di ID dei giocatori in ordine di turno, NULL se non impostato
    int player_turn; // Index del giocatore  in `player_turn_order` il cui turno è attivo
    unsigned int player_turn_order_count; // Numero di giocatori in `player_turn_order`
} GameState;


GameState *create_game_state(unsigned int game_id, const char *game_name);
int add_player_to_game_state(GameState *game, int player_id, char *username);
int remove_player_from_game_state(GameState *game, unsigned int player_id);
PlayerState *get_player_state(GameState *game, unsigned int player_id);
char *get_player_username(GameState *game, unsigned int player_id);
void free_game_state(GameState *game);

int init_board(GameBoard *board);
int set_cell(GameBoard *board, int x, int y, char value);
int is_ship_present(GameBoard *board, int x, int y);
int can_place_ship(GameBoard *board, ShipPlacement *ship);
int place_ship(GameBoard *board, ShipPlacement *ship);
int attack(PlayerState *player_state, int x, int y);

void generate_turn_order(GameState *game);

#endif // GAME_H