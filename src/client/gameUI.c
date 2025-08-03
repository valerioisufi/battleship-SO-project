#define _GNU_SOURCE
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>

#include "client/gameUI.h"
#include "common/game.h"
#include "utils/debug.h"

struct termios orig_termios;
GameScreen screen;

static void enter_alternate_screen() {
    printf("\x1b[?1049h");
}

static void exit_alternate_screen() {
    printf("\x1b[?1049l");
}

static void enable_raw_mode() {
    struct termios raw = orig_termios;
    // Disabilita echo e modalità canonica
    raw.c_lflag &= ~(ECHO | ICANON);

    // Imposta un timeout per read() per non bloccare all'infinito
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 1; // Timeout di 0.1 secondi

    // Applica le nuove impostazioni
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void hide_cursor() {
    printf("\x1b[?25l");
    fflush(stdout);
}

void show_cursor() {
    printf("\x1b[?25h");
    fflush(stdout);
}

void clear_screen() {
    printf("\x1b[2J"); // Clear the screen
    printf("\x1b[H");  // Move cursor to home position
    fflush(stdout);
}

static void update_window_size(GameScreen *screen) {
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // In caso di errore, usa valori di default o gestisci come preferisci.
        screen->width = 80;
        screen->height = 24;
        return;
    }
    screen->width = ws.ws_col;
    screen->height = ws.ws_row;

    screen->game_log.x = (screen->width - LOGS_WIDTH) / 2; // Posizione del log
    screen->game_log.y = START_LOG_Y; // Inizio del log

}

// Signal handler per il ridimensionamento della finestra.
static void handle_sigwinch(int signo) {
    update_window_size(&screen);

    refresh_screen();
}

// Funzione di restore completa, da registrare con atexit
static void restore_terminal() {
    exit_alternate_screen();
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); // Ripristina le impostazioni originali
    show_cursor(); // Ri-mostra il cursore
    fflush(stdout);
}

void init_game_interface() {
    tcgetattr(STDIN_FILENO, &orig_termios); // Salva le impostazioni correnti
    atexit(restore_terminal); // Assicura che il terminale venga ripristinato all'uscita

    enter_alternate_screen();
    enable_raw_mode(); // Attiva la modalità raw DOPO essere entrato nello schermo alternativo
    hide_cursor();
    clear_screen(); // Pulisci lo schermo alternativo per iniziare

    // Registra il gestore per SIGWINCH
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    sa.sa_handler = handle_sigwinch;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGWINCH, &sa, NULL);

    init_game_log();
    update_window_size(&screen);
}


void clear_area(int x, int y, int width, int height) {
    for (int row = 0; row < height; row++) {
        printf(MOVE_CURSOR_FORMAT, y + row + 1, x + 1);

        for (int col = 0; col < width; col++) {
            putchar(' ');
        }
    }
    fflush(stdout);
}

void draw_box(int x, int y, int width, int height) {
    // Disegna il bordo superiore
    printf(MOVE_CURSOR_FORMAT, y + 1, x + 1);
    printf("┌");
    for (int i = 0; i < width - 2; i++) {
        printf("─");
    }
    printf("┐");

    // Disegna i bordi verticali
    for (int i = 1; i < height - 1; i++) {
        printf(MOVE_CURSOR_FORMAT "│", y + i + 1, x + 1);
        printf(MOVE_CURSOR_FORMAT "│", y + i + 1, x + width);
    }

    // Disegna il bordo inferiore
    printf(MOVE_CURSOR_FORMAT, y + height, x + 1);
    printf("└");
    for (int i = 0; i < width - 2; i++) {
        printf("─");
    }
    printf("┘");

    fflush(stdout);
}

void draw_board(PlayerState *player, int x, int y) {
    for (int i = 0; i < GRID_SIZE; i++) {
        printf(MOVE_CURSOR_FORMAT "%c", y + 1, x + i*2 + 6, 'A' + i);
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        printf(MOVE_CURSOR_FORMAT "%2d", y + i + 3, x + 1, i + 1);
    }

    for (int i = 0; i < GRID_SIZE; i++) {
        for (int j = 0; j < GRID_SIZE; j++) {

            char cell = player->board.grid[j][i];
            int color = COLOR_WHITE;
            // if (cell == 'O') color = COLOR_BLUE;
            if (cell == 'X') color = COLOR_RED;
            if (cell == '*') color = COLOR_YELLOW;

            printf(MOVE_CURSOR_FORMAT, y + i + 3, x + j*2 + 6);
            printf(SET_COLOR_TEXT_FORMAT, color);

            if(cell >= 'A' && cell <= 'E') {
                printf(SET_COLOR_TEXT_BG_FORMAT " " RESET_FORMAT, color, BG_COLOR_WHITE);
                char cell_adjacent = player->board.grid[j + 1][i];
                if(cell_adjacent >= 'A' && cell_adjacent <= 'E') {
                    printf(MOVE_CURSOR_FORMAT, y + i + 3, x + j*2 + 7);
                    printf(SET_COLOR_TEXT_BG_FORMAT " " RESET_FORMAT, color, BG_COLOR_WHITE);
                } else {
                    printf(" ");
                }
            } else {
                printf(SET_COLOR_TEXT_FORMAT "%c" RESET_FORMAT, color, cell);
            }
        }
    }

    draw_box(x + 3, y + 1, GRID_SIZE * 2 + 3, GRID_SIZE + 2); // +2 for borders
    printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT, y + GRID_SIZE + 4, x + 4, COLOR_GREEN, player->username ? player->username : "Unknown Player");

    fflush(stdout);

}

void draw_legend(int x, int y) {
    printf(MOVE_CURSOR_FORMAT, y + 1, x + 1);
    printf("Legenda: ");
    printf(SET_COLOR_TEXT_FORMAT "O = Nave" RESET_FORMAT ", ", COLOR_BLUE);
    printf(SET_COLOR_TEXT_FORMAT "X = Colpito" RESET_FORMAT ", ", COLOR_RED);
    printf(SET_COLOR_TEXT_FORMAT "* = Mancato" RESET_FORMAT, COLOR_YELLOW);
    fflush(stdout);

}

void init_game_log() {
    for (int i = 0; i < LOG_SIZE; i++) {
        screen.game_log.log[i] = NULL; // Inizializza ogni voce del log a NULL
    }
    screen.game_log.last_index = -1;
    pthread_mutex_init(&screen.game_log.mutex, NULL);
}

// da utilizzare con asprintf
void log_game_message(char *message) {
    pthread_mutex_lock(&screen.game_log.mutex);
    // Aggiungi il messaggio al log, sovrascrivendo il più vecchio se necessario
    int index = (screen.game_log.last_index + 1) % LOG_SIZE;
    free(screen.game_log.log[index]); // Libera la posizione corrente
    screen.game_log.log[index] = message; // Aggiungi il nuovo messaggio
    screen.game_log.last_index = index;

    // Stampa il log aggiornato
    pthread_mutex_lock(&screen.mutex);
    clear_area(screen.game_log.x, screen.game_log.y, LOGS_WIDTH, screen.height - screen.game_log.y - 1); // Pulisci l'area del log
    print_game_log();
    pthread_mutex_unlock(&screen.mutex);

    pthread_mutex_unlock(&screen.game_log.mutex);
}

void print_game_log() {
    // Esempio di come disegnare un titolo nel riquadro
    printf(MOVE_CURSOR_FORMAT, screen.game_log.y + 1, screen.game_log.x + 2);
    for (int i = 0; i < LOGS_WIDTH - 2; i++) {
        printf("─");
    }
    // draw_box(screen.game_log.x, screen.game_log.y, LOGS_WIDTH, screen.height - screen.game_log.y - 1);
    printf(MOVE_CURSOR_FORMAT HIGHLIGHT_FORMAT " EVENTI DI GIOCO " RESET_FORMAT, screen.game_log.y + 1, screen.game_log.x + 6); // Posizionati sul bordo superiore
    // printf("Log:\n");
    for (int i = 0; i < LOG_SIZE; i++) {
        if (screen.game_log.y + i + 3 >= screen.height) {
            break; // Non disegnare oltre il limite dello schermo
        }
        printf(MOVE_CURSOR_FORMAT, screen.game_log.y + i + 2, screen.game_log.x + 4);
        int index = (screen.game_log.last_index - i + LOG_SIZE) % LOG_SIZE; // Inizia dall'ultimo messaggio
        if (screen.game_log.log[index]) {
            printf("%s", screen.game_log.log[index]);
        }
    }

    fflush(stdout);
}

PlayerState player;

void refresh_screen() {
    pthread_mutex_lock(&screen.mutex);

    clear_screen();
    if(screen.width < CONTENT_WIDTH || screen.height < START_LOG_Y + 5) {
        fprintf(stderr, "Schermo troppo piccolo per visualizzare il gioco.\n");
        pthread_mutex_unlock(&screen.mutex);
        return;
    }

    draw_box((screen.width - CONTENT_WIDTH) / 2, 0, CONTENT_WIDTH, screen.height);

    char *title = "  Battleship Game  ";
    printf(MOVE_CURSOR_FORMAT "%s", 1, (screen.width - (int)strlen(title)) / 2 + 1, title);

    int left_padding = (screen.width - GRIDS_WIDTH) / 2;
    draw_board(&player, left_padding, START_GRID_Y);
    draw_board(&player, left_padding + 30, START_GRID_Y);

    draw_legend(left_padding, START_LEGEND_Y);

    pthread_mutex_lock(&screen.game_log.mutex);
    print_game_log();
    pthread_mutex_unlock(&screen.game_log.mutex);

    pthread_mutex_unlock(&screen.mutex);
}

EscapeSequence read_escape_sequence() {
    char seq[3];

    if (getchar() == '[') {
        char c = getchar();
        switch (c) {
            case 'A': // Up
                return ESCAPE_UP;
                break;
            case 'B': // Down
                return ESCAPE_DOWN;
                break;
            case 'C': // Right
                return ESCAPE_RIGHT;
                break;
            case 'D': // Left
                return ESCAPE_LEFT;
                break;
            default: // Ignora altre sequenze
                do{
                    if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                        return ESCAPE_OTHER;
                    }
                } while(c = getchar());
                break;
        }
    }
}
void *game_ui_thread(void *arg) {
    while (1) {
        char c = getchar();
        if (c == '\x1b'){
            switch (read_escape_sequence()) {
                case ESCAPE_UP:
                    
                    break;

                case ESCAPE_DOWN:
                    /* code */
                    break;

                case ESCAPE_RIGHT:
                    /* code */
                    break;

                case ESCAPE_LEFT:
                    /* code */
                    break;

                default:
                    break;
                }
        } else {
            switch (c) {
                case 'q':
                    
                    break;
                
                default:
                    break;
            }
        }
    }

    return NULL;
}

void handle_sigintr(int signo) {
    // Gestione dell'interruzione da tastiera (Ctrl+C)
    // restore_terminal(); // Ripristina il terminale
    exit(0); // Esce dal programma
}

int main() {
    signal(SIGINT, handle_sigintr); // Assicura che il terminale venga ripristinato all'uscita
    // Esempio di utilizzo

    init_game_interface();

    // Imposta una cella
    // set_cell(&screen, 10, 5, 'A', 31, 40); // Rosso su verde

    // Disegna una box
    // draw_box(5, 5, 20, 10);
    // draw_box(1,1,4,4);

    player.user_id = 1;
    player.username = "Player1";
    init_board(&player.board);
    place_ship(&player.board, 0, 0, 4, 0); // Posiziona una nave
    place_ship(&player.board, 2, 3, 3, 1); // Posiziona un'altra nave
    if(is_ship_present(&player.board, 0, 0)) {
        log_game_message(ANSI_COLOR_GREEN "[+] Nave presente in (0, 0)\n" ANSI_COLOR_RESET);
        set_cell(&player.board, 0, 0, 'X');
    } else {
        log_game_message(ANSI_COLOR_YELLOW "[-] Nave non presente in (0, 0)" ANSI_COLOR_RESET);
        set_cell(&player.board, 0, 0, '*');
    }

    // clear_area(5, 5, 30, 15);
    refresh_screen();


    // log_game_message("Partita iniziata");
    // log_game_message("Giocatore 1 ha posizionato una nave in (0, 0)");
    // log_game_message("Giocatore 1 ha attaccato (0, 0)");
    // log_game_message("Giocatore 1 ha attaccato (2, 2)");
    // log_game_message("Giocatore 1 ha posizionato una nave in (2, 3)");
    // log_game_message("Giocatore 1 ha attaccato (2, 3)");
    // log_game_message("Giocatore 1 ha attaccato (2, 4)");
    // log_game_message("Giocatore 1 ha attaccato (3, 3)");
    // log_game_message("Giocatore 1 ha attaccato (4, 4)");
    // log_game_message("Giocatore 1 ha attaccato (5, 5)");
    // log_game_message("Giocatore 1 ha posizionato una nave in (0, 0)");
    // log_game_message("Giocatore 1 ha attaccato (0, 0)");
    // log_game_message("Giocatore 1 ha attaccato (2, 2)");
    // log_game_message("Giocatore 1 ha posizionato una nave in (2, 3)");
    // log_game_message("Giocatore 1 ha attaccato (2, 3)");
    // log_game_message("Giocatore 1 ha attaccato (2, 4)");
    // log_game_message("Giocatore 1 ha attaccato (3, 3)");
    // log_game_message("Giocatore 1 ha attaccato (4, 4)");

    while(1) pause(); // Aspetta un segnale per uscire
}