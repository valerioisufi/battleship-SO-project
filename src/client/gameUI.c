#define _GNU_SOURCE
#include <stdio.h>
#include <termios.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <string.h>
#include <pthread.h>
#include <stdarg.h>

#include "client/gameUI.h"
#include "client/clientGameManager.h"
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

    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0; // Nessun timeout

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
    pthread_mutex_lock(&screen->mutex);

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // In caso di errore, usa valori di default
        screen->width = 80;
        screen->height = 24;
        return;
    }
    screen->width = ws.ws_col;
    screen->height = ws.ws_row;

    screen->game_log.x = (screen->width - LOGS_WIDTH) / 2; // Posizione del log
    screen->game_log.y = START_LOG_Y; // Inizio del log

    pthread_mutex_unlock(&screen->mutex);

}

// Signal handler per il ridimensionamento della finestra.
static void handle_sigwinch() {
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

    pthread_mutex_init(&screen.mutex, NULL);
    init_game_log();
    update_window_size(&screen);

    screen.cursor.x = screen.cursor.y = 0; // Inizializza la posizione del cursore
    screen.cursor.x_i = screen.cursor.y_i = 0;
    screen.cursor.x_f = screen.cursor.y_f = GRID_SIZE - 1;
    screen.cursor.show = 1; // Inizialmente il cursore è visibile
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

void draw_board(PlayerState *player, int x, int y, ShipPlacement *ship_placement) {
    if (!player) {
        return;
    }

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
                if (j < GRID_SIZE - 1) {
                    char cell_adjacent = player->board.grid[j + 1][i];
                    if(cell_adjacent >= 'A' && cell_adjacent <= 'E') {
                        printf(MOVE_CURSOR_FORMAT, y + i + 3, x + j*2 + 7);
                        printf(SET_COLOR_TEXT_BG_FORMAT " " RESET_FORMAT, color, BG_COLOR_WHITE);
                    } else {
                        printf(" ");
                    }
                }
            } else {
                printf(SET_COLOR_TEXT_FORMAT "%c" RESET_FORMAT " ", color, cell);
            }
        }
    }

    if(ship_placement) {
        // Disegna la nave piazzata
        for (int i = 0; i < ship_placement->dim; i++) {
            int x_pos = ship_placement->vertical ? ship_placement->x : ship_placement->x + i;
            int y_pos = ship_placement->vertical ? ship_placement->y + i : ship_placement->y;

            if(x_pos < 0 || x_pos >= GRID_SIZE || y_pos < 0 || y_pos >= GRID_SIZE) {
                continue;
            }

            int color = can_place_ship(&player->board, ship_placement) == 0 ? BG_COLOR_GREEN : BG_COLOR_YELLOW;

            printf(MOVE_CURSOR_FORMAT, y + y_pos + 3, x + x_pos * 2 + 6);
            printf(SET_COLOR_TEXT_BG_FORMAT " " RESET_FORMAT, COLOR_WHITE, color);
        }
    }

    draw_box(x + 3, y + 1, GRID_SIZE * 2 + 3, GRID_SIZE + 2); // +2 for borders

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
void log_game_message(char *fmt, ...) {
    char *message;
    va_list args;
    va_start(args, fmt);
    if (vasprintf(&message, fmt, args) < 0) {
        va_end(args);
        exit(EXIT_FAILURE);
    }
    va_end(args);

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

ShipPlacement ship;

void refresh_board() {
    pthread_mutex_lock(&game_state_mutex);
    // clear_area((screen.width - CONTENT_WIDTH) / 2 + 1, START_GRID_Y, CONTENT_WIDTH - 2, GRID_SIZE + 3);
    int left_padding = (screen.width - GRID_WIDTH) / 2;
    switch(screen.game_screen_state) {
        case GAME_SCREEN_STATE_PLACING_SHIPS:
            draw_board(&game->players[0], left_padding, START_GRID_Y, &ship);
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " %s", START_GRID_Y + GRID_SIZE + 4, left_padding + 4, COLOR_GREEN, game->players[0].user.username ? game->players[0].user.username : "Unknown Player", "(tu)");

            break;

        case GAME_SCREEN_STATE_PLAYING:
            left_padding = (screen.width - GRID_WIDTH * 2 - GRID_PADDING) / 2;

            draw_board(&game->players[0], left_padding, START_GRID_Y, NULL);
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " %s", START_GRID_Y + GRID_SIZE + 4, left_padding + 4, COLOR_GREEN, game->players[0].user.username ? game->players[0].user.username : "Unknown Player", "(tu)");

            if(game->player_turn_order_count > 1){
                PlayerState *current_player = get_player_state(game, game->player_turn_order[screen.current_showed_player]);
                draw_board(current_player, left_padding + GRID_WIDTH + GRID_PADDING, START_GRID_Y, NULL);
                printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " %s", START_GRID_Y + GRID_SIZE + 4, left_padding + GRID_WIDTH + GRID_PADDING + 4, COLOR_GREEN, current_player->user.username ? current_player->user.username : "Unknown Player", "(avversario)");

                if(screen.cursor.show) {
                    printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT " " RESET_FORMAT, screen.cursor.y + START_GRID_Y + 3, left_padding + GRID_WIDTH + GRID_PADDING + screen.cursor.x * 2 + 6, COLOR_RED);
                }
            } else {
                // draw_board(NULL, left_padding + GRID_WIDTH + GRID_PADDING, START_GRID_Y, NULL);
                printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT, START_GRID_Y + GRID_SIZE + 4, left_padding + GRID_WIDTH + GRID_PADDING + 4, COLOR_GREEN, "Nessun avversario");
            }


            break;

        case GAME_SCREEN_STATE_FINISHED:
            // Gestisci lo stato di fine partita
            break;
    }

    pthread_mutex_unlock(&game_state_mutex);
    
}

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

    refresh_board();

    draw_legend((screen.width - CONTENT_WIDTH) / 2 + 4, START_LEGEND_Y);

    pthread_mutex_lock(&screen.game_log.mutex);
    print_game_log();
    pthread_mutex_unlock(&screen.game_log.mutex);

    pthread_mutex_unlock(&screen.mutex);
}

EscapeSequence read_escape_sequence() {
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
                } while((c = getchar()));
                break;
        }
    }

    return ESCAPE_OTHER;
}

void *game_ui_thread(void *arg) {
    GameUIArg *ui_arg = (GameUIArg *)arg;
    int pipe_fd_write = ui_arg->pipe_fd_write;
    free(ui_arg);

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH); // Sblocca SIGWINCH in questo thread
    sigaddset(&set, SIGINT); // Blocca SIGINT in questo thread
    sigaddset(&set, SIGTERM); // Blocca SIGTERM in questo thread
    pthread_sigmask(SIG_UNBLOCK, &set, NULL);

    LOG_INFO_FILE(client_log_file, "Refresh interfaccia di gioco");
    refresh_screen();

    screen.game_screen_state = GAME_SCREEN_STATE_PLACING_SHIPS;
    int ship_placed = 0;
    ship.dim = 5;
    ship.vertical = 1;

    while (1) {
        char c = getchar();

        pthread_mutex_lock(&screen.mutex);
        if (c == '\x1b'){
            switch (read_escape_sequence()) {
                case ESCAPE_UP:
                    if(screen.cursor.y > screen.cursor.y_i) screen.cursor.y--;
                    break;

                case ESCAPE_DOWN:
                    if(screen.cursor.y < screen.cursor.y_f) screen.cursor.y++;
                    break;

                case ESCAPE_RIGHT:
                    if(screen.cursor.x < screen.cursor.x_f) screen.cursor.x++;
                    break;

                case ESCAPE_LEFT:
                    if(screen.cursor.x > screen.cursor.x_i) screen.cursor.x--;
                    break;

                default:
                    break;
                }
            ship.x = screen.cursor.x;
            ship.y = screen.cursor.y;
            refresh_board();
        } else {
            switch (c) {
                case 'R':
                case 'r':
                    if (screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS) {
                        ship.vertical = !ship.vertical; // Ruota la nave
                    }
                    refresh_board();
                    break;
                case '\n':
                    if (screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS) {
                        pthread_mutex_unlock(&screen.mutex);
                        pthread_mutex_lock(&game_state_mutex);

                        if (!place_ship(&game->players[0].board, &ship)) {
                            // screen.game_screen_state = GAME_SCREEN_STATE_PLAYING;

                            game->players[0].fleet->ships[ship_placed++] = ship; // Salva la nave piazzata
                            if (ship_placed >= NUM_SHIPS) {
                                // screen.game_screen_state = GAME_SCREEN_STATE_PLAYING;

                                pthread_mutex_lock(&screen.game_log.mutex);
                                screen.cursor.show = 0; // Nascondi il cursore
                                pthread_mutex_unlock(&screen.game_log.mutex);

                                log_game_message("Tutte le navi sono state piazzate. Inizia il gioco!");
                                GameUISignal sig = GAME_UI_SIGNAL_FLEET_DEPLOYED;
                                write(pipe_fd_write, &sig, sizeof(GameUISignal));
                            } else {
                                if (ship_placed < NUM_SHIPS) {
                                    // ship.dim = 5 - ship_placed; // Aggiorna la dimensione della nave per il prossimo piazzamento
                                    if(ship_placed == 1) {
                                        ship.dim = 4; // La seconda nave è di dimensione 4
                                    } else if(ship_placed == 2) {
                                        ship.dim = 3; // La terza nave è di dimensione 3
                                    } else if(ship_placed == 3) {
                                        ship.dim = 3; // La quarta nave è di dimensione 3
                                    } else if(ship_placed == 4) {
                                        ship.dim = 2; // L'ultima nave è di dimensione 2
                                    }
                                    // screen.cursor.x = screen.cursor.y = 0; // Reset del cursore
                                    // ship.x = ship.y = 0; // Reset della posizione della nave
                                    // ship.vertical = 1; // Reset della direzione
                                }
                                log_game_message("Nave piazzata con successo. Piazza la prossima nave.");
                            }

                        } else {
                            log_game_message("Impossibile piazzare la nave. Prova un'altra posizione.");
                        }
                        pthread_mutex_unlock(&game_state_mutex);

                        pthread_mutex_lock(&screen.mutex);
                        refresh_board();
                        pthread_mutex_unlock(&screen.mutex);

                        continue;
                    } else if( screen.game_screen_state == GAME_SCREEN_STATE_PLAYING) {
                        if(game->player_turn) {
                            pthread_mutex_lock(&attack_position_mutex);
                            pthread_mutex_lock(&game_state_mutex);
                            attack_position.player_id = game->player_turn_order[screen.current_showed_player];
                            pthread_mutex_unlock(&game_state_mutex);
                            attack_position.x = screen.cursor.x;
                            attack_position.y = screen.cursor.y;

                            GameUISignal sig = GAME_UI_SIGNAL_ATTACK;
                            write(pipe_fd_write, &sig, sizeof(GameUISignal));
                            game->player_turn = 0; // Passa il turno
                        }
                    }
                    break;
                case 'S':
                    if(is_owner){
                        GameUISignal sig = GAME_UI_SIGNAL_START_GAME;
                        write(pipe_fd_write, &sig, sizeof(GameUISignal));
                    }
                    break;
                case 'Q':
                case 'q':
                    if(screen.game_screen_state == GAME_SCREEN_STATE_PLAYING){
                        screen.current_showed_player = (screen.current_showed_player + game->player_turn_order_count - 1) % game->player_turn_order_count;
                        refresh_board();
                    }
                    break;
                case 'E':
                case 'e':
                    if(screen.game_screen_state == GAME_SCREEN_STATE_PLAYING){
                        screen.current_showed_player = (screen.current_showed_player + 1) % game->player_turn_order_count;
                        refresh_board();
                    }
                    break;

                default:
                    break;
            }
        }
        
        pthread_mutex_unlock(&screen.mutex);
    }

    return NULL;
}
