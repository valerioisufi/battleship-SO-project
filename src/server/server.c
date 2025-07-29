#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>

#include <sys/socket.h>     // socket definitions
#include <sys/types.h>      // socket types
#include <arpa/inet.h>      // inet (3) functions
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>

#include <pthread.h>
#include <sys/epoll.h>
#include <errno.h>

#include "utils/cmdLineParser.h"
#include "common/protocol.h"

void *lobby_thread_main(void *arg);

int main(int argc, char *argv[]){
    signal(SIGPIPE, SIG_IGN);

    ArgvParam *allowedArgs = setArgvParams("RVport");
    parseCmdLine(argc, argv, allowedArgs);

    char *portString = getArgvParamValue("port", allowedArgs);

    char *endPtr;
    long port = strtol(portString, &endPtr, 0);
    if ( *endPtr ) {
        fprintf(stderr, "Porta non riconosciuta.\n");
        exit(EXIT_FAILURE);
    }

    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);


    /*  Create the listening socket  */
    int list_s;
    if((list_s = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        fprintf(stderr, "Errore nella creazione della socket.\n");
	    exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(list_s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        perror("setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    /*  Bind our socket addresss to the 
	listening socket, and call listen()  */
    if (bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
		fprintf(stderr, "server: errore durante la bind.\n");
		exit(EXIT_FAILURE);
    }

    if(listen(list_s, 1024) < 0) {
		fprintf(stderr, "Errore durante la listen.\n");
		exit(EXIT_FAILURE);
    }


    // Creo una pipe per comunicare con il thread della lobby
    // Il thread della lobby gestirà le connessioni dei client
    int lobby_pipe[2];
    if (pipe(lobby_pipe) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    pthread_t lobby_thread_id;
    if (pthread_create(&lobby_thread_id, NULL, lobby_thread_main, &lobby_pipe[0]) != 0) {
        perror("pthread_create lobby");
        exit(EXIT_FAILURE);
    }

    printf("Server in attesa di connessioni...\n");

    int conn_s;
    struct sockaddr_in their_addr;
    socklen_t sin_size = sizeof(struct sockaddr_in);

    // Accetta le connessioni in un ciclo infinito
    // e passa il file descriptor della connessione al thread della lobby
    // per gestire la comunicazione con il client.
    while (1) {
        if ((conn_s = accept(list_s, (struct sockaddr*)&their_addr, &sin_size)) < 0) {
            fprintf(stderr, "accept");
            continue; // Continua ad accettare altre connessioni
        }

        printf("Connessione da %s\n", inet_ntoa(their_addr.sin_addr));
        // Passa il nuovo file descriptor al thread lobby scrivendo sulla pipe
        if (write(lobby_pipe[1], &conn_s, sizeof(conn_s)) == -1) {
            perror("write to lobby pipe");
        }
    }


}

#define MAX_EVENTS 128
void *lobby_thread_main(void *arg) {
    int lobby_pipe_fd = *(int *)arg;

    int lobby_epoll_fd = epoll_create1(0);

    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = lobby_pipe_fd;
    epoll_ctl(lobby_epoll_fd, EPOLL_CTL_ADD, lobby_pipe_fd, &ev);

    while (1) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(lobby_epoll_fd, events, MAX_EVENTS, -1);

        for(int n = 0; n < nfds; n++){
            if(events[n].data.fd == lobby_pipe_fd) {
                int new_conn_s;
                if (read(lobby_pipe_fd, &new_conn_s, sizeof(new_conn_s)) == -1) {
                    perror("read from lobby pipe");
                    continue; // Continua ad accettare altre connessioni
                }

                ev.events = EPOLLIN;
                ev.data.fd = new_conn_s;
                epoll_ctl(lobby_epoll_fd, EPOLL_CTL_ADD, new_conn_s, &ev);

            }else{
                int client_s = events[n].data.fd;

                Msg *received_msg = recvMsg(client_s); // Gestisce il messaggio ricevuto dal client
                if (received_msg == NULL) {
                    fprintf(stderr, "Errore durante la ricezione del messaggio dal client %d\nChiudo la connessione con il client...\n", client_s);
                    epoll_ctl(lobby_epoll_fd, EPOLL_CTL_DEL, client_s, NULL);
                    close(client_s);
                    continue; // Continua ad accettare altri messaggi
                }
                PayloadNode *payload = parsePayload(received_msg->payload);

                switch(received_msg->header.msgType){
                    case MSG_LOGIN:
                        // Gestione del login
                        if (payload) {
                            char *username = getPayloadValue(payload, "username");
                            if (username) {
                                printf("Utente %s si è connesso.\n", username);
                                Msg *welcome_msg = createMsg(MSG_WELCOME, 0, "");
                                if(sendMsg(client_s, welcome_msg) == -1) {
                                    fprintf(stderr, "Errore durante l'invio del messaggio di benvenuto a %s.\n", username);
                                    freeMsg(welcome_msg);
                                    free(username);
                                    epoll_ctl(lobby_epoll_fd, EPOLL_CTL_DEL, client_s, NULL);
                                    close(client_s);
                                    freePayloadNodes(payload);
                                    continue; // Continua ad accettare altri messaggi
                                }

                                printf("Messaggio di benvenuto inviato a %s.\n", username);
                                
                                free(username);
                                freeMsg(welcome_msg);
                            } else {
                                fprintf(stderr, "Messaggio di login non valido.\n");
                            }
                        } else {
                            fprintf(stderr, "Payload non valido per il messaggio di login.\n");
                        }
                        break;

                    default:
                        fprintf(stderr, "Messaggio non riconosciuto: %d\n", received_msg->header.msgType);
                        Msg *error_msg = createMsg(MSG_ERROR_UNEXPECTED_MESSAGE, 0, "");
                        sendMsg(client_s, error_msg);
                        freeMsg(error_msg);
                        break;
                }

                freeMsg(received_msg);
                freePayloadNodes(payload);
            }
        }
    }

    return NULL;
}