
typedef struct {
    char *username; // Nome utente (max 30 caratteri + terminatore)
    int socket_fd;
    unsigned int id; // ID univoco dell'utente, può essere usato per identificare l'utente in modo univoco
    unsigned int game_id; // ID della partita a cui l'utente è associato, 0 se non è in una partita
} User;

typedef struct {
    char *game_name; // Nome della partita
    unsigned int game_id; // ID univoco della partita
    unsigned int owner_id; // ID dell'utente che ha creato la partita

    unsigned int *player_ids;
    unsigned int players_capacity; // Capacità attuale dell'array dei giocatori
    unsigned int players_count; // Numero attuale di giocatori nella partita
} Game;

void init_lists();

int create_user(const char *username, int socket_fd);
void remove_user(unsigned int user_id);
int update_user_socket_fd(unsigned int user_id, int socket_fd);
int get_user_socket_fd(unsigned int user_id);
int update_user_username(unsigned int user_id, const char *username);
char *get_username_by_id(unsigned int user_id);
int update_user_game_id(unsigned int user_id, unsigned int game_id);
unsigned int get_user_game_id(unsigned int user_id);

int create_game(const char *game_name, unsigned int owner_id);
void remove_game(unsigned int game_id);
int add_player_to_game(unsigned int game_id, unsigned int player_id);
int remove_player_from_game(unsigned int game_id, unsigned int player_id);