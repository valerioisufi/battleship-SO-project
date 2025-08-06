#ifndef GAME_UI_H
#define GAME_UI_H

#include "pthread.h"

#define MOVE_CURSOR_FORMAT "\x1b[%d;%dH"
#define SET_COLOR_TEXT_FORMAT "\x1b[%dm"
#define SET_COLOR_TEXT_BG_FORMAT "\x1b[%d;%dm"

#define BOLD_FORMAT "\x1b[1m"
#define ITALIC_FORMAT "\x1b[3m"
#define UNDERLINE_FORMAT "\x1b[4m"
#define HIGHLIGHT_FORMAT "\x1b[7m"
#define STRIKETHROUGH_FORMAT "\x1b[9m"

#define RESET_FORMAT "\x1b[0m"

#define COLOR_RESET      0
#define COLOR_BLACK      30
#define COLOR_RED        31
#define COLOR_GREEN      32
#define COLOR_YELLOW     33
#define COLOR_BLUE       34
#define COLOR_MAGENTA    35
#define COLOR_CYAN       36
#define COLOR_WHITE      37

#define BG_COLOR_BLACK   40
#define BG_COLOR_RED     41
#define BG_COLOR_GREEN   42
#define BG_COLOR_YELLOW  43
#define BG_COLOR_BLUE    44
#define BG_COLOR_MAGENTA 45
#define BG_COLOR_CYAN    46
#define BG_COLOR_WHITE   47


#define GRID_WIDTH 26 // Larghezza di una griglia di gioco
#define GRID_PADDING 4 // Spazio tra le griglie
#define LOGS_WIDTH (GRID_WIDTH * 2 + GRID_PADDING + 32) // Larghezza del log degli eventi
#define CONTENT_WIDTH (LOGS_WIDTH + 2) // Larghezza totale del contenuto

#define START_GRID_Y 2
#define START_LEGEND_Y (START_GRID_Y + 15) // Posizione della legenda sotto le griglie
#define START_LOG_Y (START_LEGEND_Y + 2) // Posizione del log degli eventi

#define LOG_SIZE 20

typedef struct {
    pthread_mutex_t mutex;

    char *log[LOG_SIZE];
    int last_index; // Indice dell'ultimo messaggio
    
    int x, y; // Posizione del log sullo schermo
} GameLog;

typedef enum{
    GAME_SCREEN_STATE_PLACING_SHIPS,
    GAME_SCREEN_STATE_PLAYING,
    GAME_SCREEN_STATE_FINISHED
} GameScreenState;

typedef struct{
    int x, y; // Posizione del cursore sullo schermo
    int x_i, y_i, x_f, y_f;

    int show; // Indica se il cursore deve essere visibile
} GameCursor;

typedef struct {
    pthread_mutex_t mutex; // Mutex per la sincronizzazione dell'accesso allo schermo
    
    int width; // Larghezza dello schermo
    int height; // Altezza dello schermo
    
    GameScreenState game_screen_state;
    GameCursor cursor; // Cursore per la selezione delle celle

    unsigned int current_showed_player; // Indice del giocatore attualmente visualizzato

    GameLog game_log; // Log degli eventi di gioco
} GameScreen;

typedef enum {
    ESCAPE_UP = 'A', // Freccia su
    ESCAPE_DOWN = 'B', // Freccia giù
    ESCAPE_RIGHT = 'C', // Freccia destra
    ESCAPE_LEFT = 'D', // Freccia sinistra
    ESCAPE_OTHER // Altre sequenze di escape
} EscapeSequence;

typedef struct {
    int pipe_fd_write; // File descriptor per la scrittura nella pipe;
} GameUIArg;

typedef enum {
    GAME_UI_SIGNAL_FLEET_DEPLOYED,
    GAME_UI_SIGNAL_START_GAME,
    GAME_UI_SIGNAL_ATTACK
} GameUISignal;

extern GameScreen screen;
void init_game_interface();

void init_game_log();
void log_game_message(char *fmt, ...);
void print_game_log();

void refresh_screen();

void *game_ui_thread(void *arg);

#endif // GAME_UI_H