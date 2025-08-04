#define _GNU_SOURCE
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <pthread.h>
#include <signal.h>

#include "clientGameManager.h"
#include "client/gameUI.h"
#include "utils/debug.h"
#include "utils/userInput.h"
#include "common/protocol.h"
#include "common/game.h"

GameState *game = NULL;
FILE *client_log_file = NULL;

void handle_game_msg(int conn_s, unsigned int game_id, char *game_name) {
    game = create_game_state(game_id, game_name);
    if (game == NULL) {
        LOG_ERROR("Errore nella creazione dello stato di gioco");
        exit(EXIT_FAILURE);
    }

    client_log_file = fopen("client.log", "w+");
    if (!client_log_file) {
        LOG_ERROR("Errore nell'apertura del file di log client.log, usando stderr");
        client_log_file = stderr; // Fallback to stderr if log file cannot be opened
    } else {
        atexit(print_log_file);
    }

    LOG_INFO_FILE(client_log_file, "Iniziando la partita `%s` con ID %d", game_name, game_id);

    // Avvio il thread dell'interfaccia di gioco
    // NOTA: da questo punto in poi, il thread dell'interfaccia di gioco gestirà l'input dell'utente
    // e per printare i messaggi di gioco è necessario utilizzare la funzione `log_game_message`
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGWINCH); // Blocca SIGWINCH in questo thread
    sigaddset(&set, SIGINT); // Blocca SIGINT in questo thread
    sigaddset(&set, SIGTERM); // Blocca SIGTERM in questo thread
    pthread_sigmask(SIG_BLOCK, &set, NULL);

    init_game_interface();

    pthread_t game_thread_id;
    if(pthread_create(&game_thread_id, NULL, game_ui_thread, (void *)conn_s) != 0) {
        LOG_ERROR_FILE(client_log_file, "Errore durante la creazione del thread di gioco");
        exit(EXIT_FAILURE);
    }
    pthread_detach(game_thread_id); // Il thread dell'interfaccia di gioco si occuperà della sua terminazione

    log_game_message("Benvenuto nella partita `%s` con ID %d", game_name, game_id);

    if(safeSendMsg(conn_s, MSG_READY_TO_PLAY, NULL)){
        LOG_ERROR_FILE(client_log_file, "Errore durante l'invio di MSG_READY_TO_PLAY al server");
        exit(EXIT_FAILURE);
    }

    while(1) {
        uint16_t msg_type;
        Payload *payload = NULL;
        if (safeRecvMsg(conn_s, &msg_type, &payload) < 0) {
            LOG_ERROR_FILE(client_log_file, "Errore durante la ricezione del messaggio di gioco dal server");
            break;
        }

        switch (msg_type) {
            case MSG_GAME_STATE_UPDATE: {
                // Gestisci l'aggiornamento dello stato del gioco
                int payload_size = getPayloadListSize(payload);
                for(int i = 0; i < payload_size; i++) {
                    char *key = getPayloadValue(payload, i, "type");
                    if (key == NULL) {
                        LOG_ERROR_FILE(client_log_file, "Tipo di messaggio non specificato nel payload");
                        continue;
                    }

                    if (strcmp(key, "game_info") == 0) {
                        // Gestisci le informazioni del gioco
                        continue;
                    } else if (strcmp(key, "player_info") == 0) {
                        // Gestisci le informazioni del giocatore
                        int player_id;
                        if (getPayloadIntValue(payload, i, "player_id", &player_id) < 0) {
                            LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
                            continue;
                        }
                        char *username = getPayloadValue(payload, i, "username");
                        if (username == NULL) {
                            LOG_ERROR_FILE(client_log_file, "Nome utente non trovato nel payload");
                            continue;
                        }
                        log_game_message("Giocatore %d (`%s`) si è unito alla partita %d", player_id, username, game->game_id);
                        add_player_to_game_state(game, player_id, username);

                        free(username);
                    }

                    free(key);
                }

                break;
            }
            case MSG_PLAYER_JOINED: {
                int player_id;
                if (getPayloadIntValue(payload, 0, "player_id", &player_id) < 0) {
                    LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
                    freePayload(payload);
                    continue;
                }
                char *username = getPayloadValue(payload, 0, "username");
                if(username == NULL) {
                    LOG_ERROR_FILE(client_log_file, "Nome utente non trovato nel payload");
                    freePayload(payload);
                    continue;
                }

                log_game_message("Giocatore %d (`%s`) si è unito alla partita %d", player_id, username, game->game_id);
                add_player_to_game_state(game, player_id, username);

                free(username);
                break;
            }
            case MSG_PLAYER_ACTION: {
                int player_id;
                if (getPayloadIntValue(payload, 0, "player_id", &player_id) < 0) {
                    LOG_ERROR_FILE(client_log_file, "ID del giocatore non trovato nel payload");
                    freePayload(payload);
                    continue;
                }

                if (handle_player_action(player_id, payload) < 0) {
                    LOG_ERROR_FILE(client_log_file, "Errore nella gestione dell'azione del giocatore %d", player_id);
                }
                break;
            }
            default:
                handle_generic_msg(msg_type, payload);
                break;
        }

        freePayload(payload);
    }

    LOG_INFO_FILE(client_log_file, "Chiusura della partita `%s` con ID %d", game_name, game_id);
    exit(0);
}

int handle_player_action(int player_id, Payload *payload) {
    char *action = getPayloadValue(payload, 0, "action");
    if (action == NULL) {
        LOG_ERROR("Azione non specificata per il giocatore %d", player_id);
        return -1;
    }

    // if (strcmp(action, "attack") == 0) {
    //     int x = atoi(getPayloadValue(payload, 0, "x"));
    //     int y = atoi(getPayloadValue(payload, 0, "y"));
    //     int adversary_id = atoi(getPayloadValue(payload, 0, "adversary_id"));

    //     PlayerState *player_state = get_player_state(game, player_id);
    //     PlayerState *adversary_state = get_player_state(game, adversary_id);
    //     if (player_state == NULL || adversary_state == NULL) {
    //         LOG_ERROR("Giocatore %d o avversario %d non trovato nella partita.", player_id, adversary_id);
    //         free(action);
    //         return -1;
    //     }

    //     if (attack(&adversary_state->board, x, y) < 0) {
    //         LOG_ERROR("Errore durante l'attacco del giocatore %d alla posizione (%d, %d)", adversary_id, x, y);
    //         free(action);
    //         return -1;
    //     }
    // } else {
    //     LOG_ERROR("Azione '%s' non riconosciuta per il giocatore %d", action, player_id);
    // }

    free(action);
    return 0;
}

/**
 * Gestisce i messaggi generici ricevuti dal server.
 * @param msg_type Il tipo di messaggio.
 * @param payload Il payload del messaggio.
 */
void handle_generic_msg(uint16_t msg_type, Payload *payload) {

    switch(msg_type) {
        case MSG_ERROR_UNEXPECTED_MESSAGE:
            LOG_ERROR_FILE(client_log_file, "Messaggio non riconosciuto ricevuto dal server");
            break;

        case MSG_ERROR_MALFORMED_MESSAGE:
            LOG_ERROR_FILE(client_log_file, "Messaggio malformato ricevuto dal server");
            break;

        case MSG_ERROR_NOT_AUTHENTICATED:
            LOG_ERROR_FILE(client_log_file, "Messaggio di errore: non autenticato");
            break;

        default:
            LOG_WARNING_FILE(client_log_file, "Messaggio non riconosciuto: %d", msg_type);
            break;
    }
    freePayload(payload);
}

void print_log_file() {
    if (client_log_file && client_log_file != stderr) {
        fseek(client_log_file, 0, SEEK_SET);

        char buffer[256];
        LOG_INFO("Contenuto del file di log client.log:");
        while (fgets(buffer, sizeof(buffer), client_log_file) != NULL) {
            printf("%s", buffer);
        }

        fclose(client_log_file);
        client_log_file = NULL;
    }
    LOG_INFO("File di log chiuso correttamente.");
}