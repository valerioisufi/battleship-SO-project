#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>

#include "utils/cmdLineParser.h"
#include "utils/debug.h"
#include "utils/userInput.h"
#include "common/protocol.h"
#include "client/clientGameManager.h"

void menu(int conn_s);
void cleanup_on_exit();
void cleanup_and_exit_handler();

int conn_socket_for_exit = -1;

int main(int argc, char *argv[]){
    atexit(cleanup_on_exit); // Registra la funzione di cleanup per chiudere il socket
    signal(SIGINT, cleanup_and_exit_handler); // Gestisce l'interruzione da tastiera (Ctrl+C)
    signal(SIGTERM, cleanup_and_exit_handler); // Gestisce la terminazione del processo
    signal(SIGPIPE, SIG_IGN);

    ArgvParam *allowedArgs = setArgvParams("RVaddress,RVport");
    parseCmdLine(argc, argv, allowedArgs);

    char *addressString = getArgvParamValue("address", allowedArgs);
    char *portString = getArgvParamValue("port", allowedArgs);

    int port;
    if (getIntFromString(portString, &port) != 0) {
        LOG_ERROR("Porta non riconosciuta");
        exit(EXIT_FAILURE);
    }
    
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_port = htons(port);

    struct hostent *he = NULL;
    if(inet_aton(addressString, &servaddr.sin_addr) <= 0){

        if((he = gethostbyname(addressString)) == NULL){
            LOG_ERROR("Indirizzo IP non valido, risoluzione nome fallita");
            exit(EXIT_FAILURE);
        }

        servaddr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);
    }

    int conn_s; // connection socket
    if((conn_s = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        LOG_ERROR("Errore durante la creazione della socket");
        exit(EXIT_FAILURE);
    }

    conn_socket_for_exit = conn_s; // Salvo il socket per la chiusura in caso di errore
    
    if(connect(conn_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0){
        LOG_ERROR("Errore durante la connect");
        exit(EXIT_FAILURE);
    }


    // richiedo il nome dell'utente
    printf("Inserire un nome utente (max 30 caratteri): ");
    char *username = readAlfanumericString(30);
    if(username == NULL){
        LOG_ERROR("Errore durante la lettura del nome utente");
        exit(EXIT_FAILURE);
    }


    // effettuo il login (MSG_LOGIN)
    Payload *loginPayload = createEmptyPayload();
    addPayloadKeyValuePair(loginPayload, "username", username); // Client type C for client

    if(safeSendMsg(conn_s, MSG_LOGIN, loginPayload) < 0){
        LOG_ERROR("Errore durante l'invio del messaggio di login al server");
        exit(EXIT_FAILURE);
    }

    // attendo la risposta dal server (MSG_WELCOME)
    uint16_t msg_type;
    Payload *payload = NULL;
    if(safeRecvMsg(conn_s, &msg_type, &payload) < 0){
        LOG_ERROR("Errore durante la ricezione del messaggio di benvenuto dal server");
        exit(EXIT_FAILURE);
    }
    

    switch(msg_type){
        case MSG_WELCOME:
            user = malloc(sizeof(UserInfo));
            user->username = strdup(username); // Salvo il nome utente per l'uso futuro
            if(getPayloadIntValue(payload, 0, "user_id", (int *)&user->user_id)){
                LOG_ERROR("ID dell'utente non trovato nel payload");
                free(username);
                freePayload(payload);
                exit(EXIT_FAILURE);
            }
            free(username); // Libero la memoria del nome utente
            freePayload(payload);

            printf("Benvenuto nel gioco %s!\n", user->username);
            menu(conn_s);
            break;

        default:
            LOG_WARNING("Messaggio non riconosciuto: %d", msg_type);
            freePayload(payload);
            exit(EXIT_FAILURE);
    }


    pause();

    freeArgvParams(allowedArgs);
    return 0;

}


void menu(int conn_s){
    uint16_t msg_type;
    Payload *payload = NULL;

    while(1){
        printf("\n1. Inizia una nuova partita\n");
        printf("2. Unisciti a una partita esistente\n");
        printf("3. Esci\n");
        printf("\nSeleziona un'opzione: ");

        int choice, ret;
        if((ret = scanf("%d", &choice)) != 1) {
            if(ret == EOF) {
                LOG_ERROR("Errore durante la lettura dell'input");
                exit(EXIT_FAILURE);
            }
            LOG_ERROR("Input non valido");
        }
        eof_handler(conn_s);
        fflush(stdin);

        switch(choice){
            case 1:
                printf("Iniziando una nuova partita...\n");
                printf("Inserisci il nome della partita: ");
                char *game_name = readAlfanumericString(30);
                if(game_name == NULL){
                    LOG_ERROR("Errore durante la lettura del nome della partita");
                    exit(EXIT_FAILURE);
                }

                Payload *createGamePayload = createEmptyPayload();
                addPayloadKeyValuePair(createGamePayload, "game_name", game_name);
                if(safeSendMsg(conn_s, MSG_CREATE_GAME, createGamePayload) < 0){
                    LOG_ERROR("Errore durante l'invio del messaggio di creazione della partita al server");
                    exit(EXIT_FAILURE);
                }

                if(safeRecvMsg(conn_s, &msg_type, &payload) < 0){
                    LOG_ERROR("Errore durante la ricezione del messaggio di creazione della partita dal server");
                    exit(EXIT_FAILURE);
                }
                if(msg_type == MSG_GAME_CREATED){
                    int game_id;
                    if(getPayloadIntValue(payload, 0, "game_id", &game_id) != -1){
                        printf("Partita creata con successo! ID: %d\n", game_id);
                        is_owner = 1;
                        handle_game_msg(conn_s, game_id, game_name);
                    } else {
                        LOG_ERROR("ID della partita non trovato nel payload o non valido");
                    }

                } else if(msg_type == MSG_ERROR_CREATE_GAME){
                    LOG_ERROR("Errore durante la creazione della partita");
                } else {
                    LOG_WARNING("Messaggio non riconosciuto: %d", msg_type);
                }
                free(game_name);
                freePayload(payload);

                break;

            case 2:
                printf("Unendosi a una partita esistente...\n");
                printf("Inserisci il codice della partita: ");
                char *game_id_str = readAlfanumericString(10);
                if(game_id_str == NULL){
                    LOG_ERROR("Errore durante la lettura del codice della partita");
                    exit(EXIT_FAILURE);
                }
                int game_id;
                if(getIntFromString(game_id_str, &game_id) != 0){
                    LOG_ERROR("ID della partita non valido");
                    free(game_id_str);
                    continue; // Torna al menu
                }

                Payload *joinGamePayload = createEmptyPayload();
                addPayloadKeyValuePair(joinGamePayload, "game_id", game_id_str);
                free(game_id_str);
                if(safeSendMsg(conn_s, MSG_JOIN_GAME, joinGamePayload) < 0){
                    LOG_ERROR("Errore durante l'invio del messaggio di unione alla partita al server");
                    exit(EXIT_FAILURE);
                }

                if(safeRecvMsg(conn_s, &msg_type, &payload) < 0){
                    LOG_ERROR("Errore durante la ricezione del messaggio di unione alla partita dal server");
                    exit(EXIT_FAILURE);
                }
                if(msg_type == MSG_GAME_JOINED){
                    char *game_name = getPayloadValue(payload, 0, "game_name");
                    if(game_name == NULL){
                        LOG_ERROR("Nome della partita non trovato nel payload");
                    } else {
                        // printf("Unito alla partita con successo! Nome: %s, ID: %d\n", game_name, game_id);
                        handle_game_msg(conn_s, game_id, game_name);
                    }

                } else if(msg_type == MSG_ERROR_JOIN_GAME){
                    LOG_ERROR("Errore durante l'unione alla partita");
                } else {
                    LOG_WARNING("Messaggio non riconosciuto: %d", msg_type);
                }

                freePayload(payload);

                break;

            case 3:
                printf("Uscita dal gioco...\n");
                exit(0);
                break;

            default:
                printf("Opzione non valida.\n");
                break;
        }
    }
}

void cleanup_on_exit() {
    if (conn_socket_for_exit >= 0) {
        LOG_INFO("Chiusura della connessione...");
        close(conn_socket_for_exit);
        conn_socket_for_exit = -1;
    }
}

void cleanup_and_exit_handler() {
    exit(0);
}