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

volatile sig_atomic_t resized = 0;


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

static void hide_cursor() {
    printf("\x1b[?25l");
    fflush(stdout);
}

static void show_cursor() {
    printf("\x1b[?25h");
    fflush(stdout);
}

static void clear_screen() {
    printf("\x1b[2J"); // Clear the screen
    printf("\x1b[H");  // Move cursor to home position
    fflush(stdout);
}

/**
 * Aggiorna le dimensioni della finestra di gioco.
 */
static void update_window_size(GameScreen *screen) {
    pthread_mutex_lock(&screen->mutex);

    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        // In caso di errore, usa valori di default
        screen->width = 80;
        screen->height = 24;
    } else {
        screen->width = ws.ws_col;
        screen->height = ws.ws_row;
    }
    
    screen->game_log.x = (screen->width - LOGS_WIDTH) / 2; // Posizione del log
    screen->game_log.y = START_LOG_Y; // Inizio del log

    pthread_mutex_unlock(&screen->mutex);

}

// Signal handler per il ridimensionamento della finestra.
static void handle_sigwinch(int sig) {
    (void)sig;
    resized = 1;
}

// Funzione di restore completa, da registrare con atexit
static void restore_terminal() {
    exit_alternate_screen();
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios); // Ripristina le impostazioni originali
    show_cursor(); // Ri-mostra il cursore
    fflush(stdout);
}

/**
 * Inizializza l'interfaccia di gioco.
 */
void init_game_interface() {
    tcgetattr(STDIN_FILENO, &orig_termios); // Salva le impostazioni correnti
    atexit(restore_terminal); // Assicura che il terminale venga ripristinato all'uscita

    enter_alternate_screen();
    enable_raw_mode();
    hide_cursor();
    clear_screen();

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


/**
 * Pulisce un'area dello schermo.
 * @param x La coordinata x dell'angolo superiore sinistro dell'area da pulire.
 * @param y La coordinata y dell'angolo superiore sinistro dell'area da pulire.
 * @param width La larghezza dell'area da pulire.
 * @param height L'altezza dell'area da pulire.
 */
void clear_area(int x, int y, int width, int height) {
    for (int row = 0; row < height; row++) {
        printf(MOVE_CURSOR_FORMAT, y + row + 1, x + 1);

        for (int col = 0; col < width; col++) {
            putchar(' ');
        }
    }
    fflush(stdout);
}

/**
 * Disegna un riquadro sullo schermo.
 * @param x La coordinata x dell'angolo superiore sinistro del riquadro.
 * @param y La coordinata y dell'angolo superiore sinistro del riquadro.
 * @param width La larghezza del riquadro.
 * @param height L'altezza del riquadro.
 */
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

/**
 * Disegna la griglia di gioco per un giocatore.
 * @param player Il giocatore di cui disegnare la griglia.
 * @param x La coordinata x dell'angolo superiore sinistro della griglia.
 * @param y La coordinata y dell'angolo superiore sinistro della griglia.
 * @param ship_placement Le informazioni sul posizionamento delle navi.
 */
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

    if(ship_placement && screen.cursor.show) {
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

/**
 * Disegna la legenda della griglia di gioco.
 * @param x La coordinata x dell'angolo superiore sinistro della legenda.
 * @param y La coordinata y dell'angolo superiore sinistro della legenda.
 */
void draw_legend(int x, int y) {
    printf(MOVE_CURSOR_FORMAT, y + 1, x + 1);

    if(screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS || screen.game_screen_state == GAME_SCREEN_STATE_PLAYING) {
        printf(SET_COLOR_TEXT_FORMAT "X" RESET_FORMAT "=Colpito, ", COLOR_RED);
        printf(SET_COLOR_TEXT_FORMAT "*" RESET_FORMAT "=Mancato", COLOR_YELLOW);
        printf("    |    ");
    }

    if (screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS) {
        printf(SET_COLOR_TEXT_FORMAT "Frecce" RESET_FORMAT ":muovi  ", COLOR_BLUE);
        printf(SET_COLOR_TEXT_FORMAT "R" RESET_FORMAT ":ruota  ", COLOR_GREEN);
        printf(SET_COLOR_TEXT_FORMAT "Invio" RESET_FORMAT ":piazza  ", COLOR_YELLOW);
        if (is_owner)
            printf(SET_COLOR_TEXT_FORMAT "S" RESET_FORMAT ":avvia", COLOR_MAGENTA);
    } else if (screen.game_screen_state == GAME_SCREEN_STATE_PLAYING) {
        printf(SET_COLOR_TEXT_FORMAT "Frecce" RESET_FORMAT ":seleziona  ", COLOR_BLUE);
        printf(SET_COLOR_TEXT_FORMAT "Invio" RESET_FORMAT ":attacca  ", COLOR_YELLOW);
        printf(SET_COLOR_TEXT_FORMAT "Q/E" RESET_FORMAT ":scorri", COLOR_CYAN);
    }

    fflush(stdout);
}

/**
 * Inizializza il log di gioco.
 */
void init_game_log() {
    for (int i = 0; i < LOG_SIZE; i++) {
        screen.game_log.log[i] = NULL; // Inizializza ogni voce del log a NULL
    }
    screen.game_log.last_index = -1;
    pthread_mutex_init(&screen.game_log.mutex, NULL);
}

/**
 * Registra un messaggio nel log di gioco.
 * @param fmt Il formato del messaggio di log.
 * @param ... I valori da inserire nel formato.
 */
void log_game_message(char *fmt, ...) {
    char *message;
    va_list args;
    va_start(args, fmt);
    if (vasprintf(&message, fmt, args) < 0) {
        va_end(args);
        exit(EXIT_FAILURE);
    }
    va_end(args);

    // Ordine di lock coerente: prima screen.mutex, poi game_log.mutex
    pthread_mutex_lock(&screen.mutex);
    pthread_mutex_lock(&screen.game_log.mutex);
    // Aggiungi il messaggio al log, sovrascrivendo il più vecchio se necessario
    int index = (screen.game_log.last_index + 1) % LOG_SIZE;
    free(screen.game_log.log[index]); // Libera la posizione corrente
    screen.game_log.log[index] = message; // Aggiungi il nuovo messaggio
    screen.game_log.last_index = index;

    // Stampa il log aggiornato
    clear_area(screen.game_log.x, screen.game_log.y, LOGS_WIDTH, screen.height - screen.game_log.y - 1); // Pulisci l'area del log
    print_game_log();
    pthread_mutex_unlock(&screen.game_log.mutex);
    pthread_mutex_unlock(&screen.mutex);
}

/**
 * Stampa i log di gioco.
 */
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

/**
 * Aggiorna la griglia di gioco.
 */
void refresh_board() {
    int left_padding = (screen.width - GRID_WIDTH) / 2;
    switch(screen.game_screen_state) {
        case GAME_SCREEN_STATE_PLACING_SHIPS: {
            draw_board(&game->players[0], left_padding, START_GRID_Y, &ship);
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " %s", START_GRID_Y + GRID_SIZE + 4, left_padding + 4, COLOR_GREEN, game->players[0].user.username ? game->players[0].user.username : "Unknown Player", "(tu)");

            break;
}
        case GAME_SCREEN_STATE_PLAYING: {
            left_padding = (screen.width - GRID_WIDTH * 2 - GRID_PADDING) / 2;

            draw_board(&game->players[0], left_padding, START_GRID_Y, NULL);
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " %s", START_GRID_Y + GRID_SIZE + 4, left_padding + 4, COLOR_GREEN, game->players[0].user.username ? game->players[0].user.username : "Unknown Player", "(tu)");

            if(game->player_turn_order_count > 1){
                PlayerState *current_player = get_player_state(game, game->player_turn_order[screen.current_showed_player]);

                if (current_player != NULL) {
                    // Se il giocatore esiste, disegna la sua board
                    draw_board(current_player, left_padding + GRID_WIDTH + GRID_PADDING, START_GRID_Y, NULL);
                    char *player_name = current_player->user.username ? current_player->user.username : "Unknown Player";
                    int player_name_len = strlen(player_name);
                    printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " (%d/%d)", START_GRID_Y + GRID_SIZE + 4, left_padding + GRID_WIDTH + GRID_PADDING + 4, COLOR_GREEN, player_name, screen.current_showed_player + 1, game->player_turn_order_count);
                    for(int i = 0; i < 20 - player_name_len; i++) {
                        printf(" ");
                    }

                    if(screen.cursor.show) {
                        printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT " " RESET_FORMAT, screen.cursor.y + START_GRID_Y + 3, left_padding + GRID_WIDTH + GRID_PADDING + screen.cursor.x * 2 + 6, COLOR_RED);
                    }
                } else {
                    // Altrimenti, mostra un messaggio che indica che il giocatore è stato eliminato
                    int board_x = left_padding + GRID_WIDTH + GRID_PADDING;
                    int board_y = START_GRID_Y;
                    clear_area(board_x, board_y, GRID_WIDTH, GRID_SIZE + 5); // Pulisce l'area della board
                    printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT, START_GRID_Y + GRID_SIZE + 4, board_x + 4, COLOR_RED, "Giocatore Eliminato");
                }
            } else {
                printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT, START_GRID_Y + GRID_SIZE + 4, left_padding + GRID_WIDTH + GRID_PADDING + 4, COLOR_GREEN, "Nessun avversario");
            }

            break;
        }
        case GAME_SCREEN_STATE_ELIMINATED: {
            // Mostra la board finale del giocatore locale e un messaggio di eliminazione
            left_padding = (screen.width - GRID_WIDTH * 2 - GRID_PADDING) / 2;
            draw_board(&game->players[0], left_padding, START_GRID_Y, NULL);
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT " %s", START_GRID_Y + GRID_SIZE + 4, left_padding + 4, COLOR_GREEN, game->players[0].user.username ? game->players[0].user.username : "Unknown Player", "(tu)");
            
            char *msg = "SEI STATO ELIMINATO!";
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT,
                START_GRID_Y + GRID_SIZE / 2 + 2,
                left_padding + GRID_WIDTH + GRID_PADDING + 4,
                COLOR_RED, msg);
            break;
        }
        case GAME_SCREEN_STATE_FINISHED:{
            // Gestisci lo stato di fine partita
            char *msg = "PARTITA TERMINATA!";
            printf(MOVE_CURSOR_FORMAT SET_COLOR_TEXT_FORMAT HIGHLIGHT_FORMAT "%s" RESET_FORMAT,
                START_GRID_Y + GRID_SIZE / 2 + 4,
                (screen.width - (int)strlen(msg)) / 2,
                COLOR_CYAN, msg);
            break;
        }
    }

    fflush(stdout);
}

/**
 * Aggiorna la schermata di gioco.
 */
void refresh_screen() {
    pthread_mutex_lock(&game_state_mutex);
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
    pthread_mutex_unlock(&game_state_mutex);
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
    sigaddset(&set, SIGWINCH);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_UNBLOCK, &set, NULL); // Sblocca i segnali in questo thread

    LOG_INFO_FILE(client_log_file, "Refresh interfaccia di gioco");
    refresh_screen();

    pthread_mutex_lock(&screen.mutex);
    screen.game_screen_state = GAME_SCREEN_STATE_PLACING_SHIPS;
    pthread_mutex_unlock(&screen.mutex);

    int ship_placed = 0;
    ship.dim = SHIP_PLACEMENT_SEQUENCE[ship_placed];
    ship.vertical = 1;

    while (1) {
        if (resized) {
            update_window_size(&screen);
            refresh_screen();
            resized = 0;
        }
        char c = getchar();

        if (screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS || screen.game_screen_state == GAME_SCREEN_STATE_PLAYING) {
            if (c == '\x1b'){
                pthread_mutex_lock(&game_state_mutex);
                pthread_mutex_lock(&screen.mutex);
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
                pthread_mutex_unlock(&screen.mutex);
                pthread_mutex_unlock(&game_state_mutex);
            } else {
                switch (c) {
                    case 'R':
                    case 'r':
                        pthread_mutex_lock(&game_state_mutex);
                        pthread_mutex_lock(&screen.mutex);
                        if (screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS) {
                            ship.vertical = !ship.vertical; // Ruota la nave
                        }
                        refresh_board();
                        pthread_mutex_unlock(&screen.mutex);
                        pthread_mutex_unlock(&game_state_mutex);
                        break;
                    case '\n':
                        pthread_mutex_lock(&screen.mutex);
                        if (screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS && ship_placed < NUM_SHIPS) {
                            pthread_mutex_unlock(&screen.mutex);
                            int placed_ok = 0;
                            pthread_mutex_lock(&game_state_mutex);
                            if (!place_ship(&game->players[0].board, &ship)) {
                                game->players[0].fleet->ships[ship_placed++] = ship; // Salva la nave piazzata
                                placed_ok = 1;
                            }
                            pthread_mutex_unlock(&game_state_mutex);

                            if (placed_ok) {
                                if (ship_placed >= NUM_SHIPS) {
                                    // screen.game_screen_state = GAME_SCREEN_STATE_PLAYING;

                                    pthread_mutex_lock(&screen.mutex);
                                    screen.cursor.show = 0; // Nascondi il cursore
                                    pthread_mutex_unlock(&screen.mutex);

                                    if (is_owner) {
                                        log_game_message("Flotta schierata! Premi 'S' per iniziare la partita.");
                                    } else {
                                        log_game_message("Flotta schierata! Attendi che il proprietario avvii la partita.");
                                    }

                                    GameUISignal sig;
                                    memset(&sig, 0, sizeof(sig));
                                    sig.type = GAME_UI_SIGNAL_FLEET_DEPLOYED;
                                    sig.data = NULL;
                                    write(pipe_fd_write, &sig, sizeof(GameUISignal));
                                } else {
                                    int old_dim = ship.dim;
                                    ship.dim = SHIP_PLACEMENT_SEQUENCE[ship_placed]; // Aggiorna alla dimensione della nave successiva
                                    log_game_message("Nave da %d piazzata. Ora posiziona la nave da %d.", old_dim, ship.dim);
                                }
                            } else {
                                log_game_message(SET_COLOR_TEXT_FORMAT "Posizione non valida!" RESET_FORMAT " La nave si sovrappone o è fuori griglia.", COLOR_YELLOW);
                            }
                            
                            pthread_mutex_lock(&game_state_mutex);
                            pthread_mutex_lock(&screen.mutex);
                            refresh_board();
                            pthread_mutex_unlock(&screen.mutex);
                            pthread_mutex_unlock(&game_state_mutex);
                            
                            continue;
                        } else if( screen.game_screen_state == GAME_SCREEN_STATE_PLAYING) {
                            pthread_mutex_unlock(&screen.mutex);

                            pthread_mutex_lock(&game_state_mutex);
                            pthread_mutex_lock(&screen.mutex);
                            if(game->player_turn == local_player_turn_index && screen.cursor.show) {
                                int player_id = game->player_turn_order[screen.current_showed_player];
                                PlayerState *current_player = get_player_state(game, player_id);

                                if(current_player == NULL) {
                                    pthread_mutex_unlock(&screen.mutex);
                                    log_game_message(SET_COLOR_TEXT_FORMAT "Il giocatore non esiste!" RESET_FORMAT " Cambia griglia visualizzata.", COLOR_RED);
                                    pthread_mutex_unlock(&game_state_mutex);
                                    continue;
                                }

                                if(current_player->board.grid[screen.cursor.x][screen.cursor.y] == '.'){
                                    AttackPosition *attack_position = malloc(sizeof(AttackPosition));
                                    attack_position->player_id = player_id;
                                    attack_position->x = screen.cursor.x;
                                    attack_position->y = screen.cursor.y;

                                    GameUISignal sig;
                                    memset(&sig, 0, sizeof(sig));
                                    sig.type = GAME_UI_SIGNAL_ATTACK;
                                    sig.data = attack_position;
                                    write(pipe_fd_write, &sig, sizeof(GameUISignal));

                                    screen.cursor.show = 0; // Nascondi il cursore dopo l'attacco
                                    refresh_board();
                                } else {
                                    pthread_mutex_unlock(&screen.mutex);
                                    log_game_message(SET_COLOR_TEXT_FORMAT "Cella già colpita!" RESET_FORMAT " Scegli un'altra coordinata.", COLOR_YELLOW);
                                    pthread_mutex_lock(&screen.mutex);
                                }
                            }
                            pthread_mutex_unlock(&screen.mutex);
                            pthread_mutex_unlock(&game_state_mutex);
                        } else {
                            pthread_mutex_unlock(&screen.mutex);
                        }
                        break;
                    case 'S':
                    case 's':
                        pthread_mutex_lock(&screen.mutex);
                        if(is_owner && screen.game_screen_state == GAME_SCREEN_STATE_PLACING_SHIPS) {
                            GameUISignal sig;
                            memset(&sig, 0, sizeof(sig));
                            sig.type = GAME_UI_SIGNAL_START_GAME;
                            sig.data = NULL;
                            write(pipe_fd_write, &sig, sizeof(GameUISignal));
                        }
                        pthread_mutex_unlock(&screen.mutex);
                        break;
                    case 'Q':
                    case 'q':
                        pthread_mutex_lock(&game_state_mutex);
                        pthread_mutex_lock(&screen.mutex);
                        if(screen.game_screen_state == GAME_SCREEN_STATE_PLAYING){
                            do{
                                screen.current_showed_player = (screen.current_showed_player + game->player_turn_order_count - 1) % game->player_turn_order_count;
                            } while(game->player_turn_order[screen.current_showed_player] == -1 || (int)screen.current_showed_player == local_player_turn_index);
                            refresh_board();
                        }
                        pthread_mutex_unlock(&screen.mutex);
                        pthread_mutex_unlock(&game_state_mutex);
                        break;
                    case 'E':
                    case 'e':
                        pthread_mutex_lock(&game_state_mutex);
                        pthread_mutex_lock(&screen.mutex);
                        if(screen.game_screen_state == GAME_SCREEN_STATE_PLAYING){
                            do{
                                screen.current_showed_player = (screen.current_showed_player + 1) % game->player_turn_order_count;
                            } while(game->player_turn_order[screen.current_showed_player] == -1 || (int)screen.current_showed_player == local_player_turn_index);
                            refresh_board();
                        }
                        pthread_mutex_unlock(&screen.mutex);
                        pthread_mutex_unlock(&game_state_mutex);
                        break;

                    default:
                        break;
                }
            }
        }
        
    }

    return NULL;
}
