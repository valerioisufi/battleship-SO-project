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
#include "utils/debug.h"
#include "common/protocol.h"
#include "server/users.h"
#include "server/lobbyManager.h"

void *lobby_thread_main(void *arg);

int main(int argc, char *argv[]){
    signal(SIGPIPE, SIG_IGN);

    init_lists();

    ArgvParam *allowedArgs = setArgvParams("RVport");
    parseCmdLine(argc, argv, allowedArgs);

    char *portString = getArgvParamValue("port", allowedArgs);

    char *endPtr;
    long port = strtol(portString, &endPtr, 0);
    if ( *endPtr ) {
        LOG_ERROR("Porta non riconosciuta");
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
        LOG_ERROR("Errore nella creazione della socket");
        exit(EXIT_FAILURE);
    }

    int optval = 1;
    if (setsockopt(list_s, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) < 0) {
        LOG_DEBUG_ERROR("Errore in setsockopt SO_REUSEADDR");
        exit(EXIT_FAILURE);
    }

    /*  Bind our socket addresss to the 
    listening socket, and call listen()  */
    if (bind(list_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0 ) {
        LOG_ERROR("Errore durante la bind");
        exit(EXIT_FAILURE);
    }

    if(listen(list_s, 1024) < 0) {
        LOG_ERROR("Errore durante la listen");
        exit(EXIT_FAILURE);
    }


    // Creo una pipe per comunicare con il thread della lobby
    // Il thread della lobby gestirà le connessioni dei client
    int lobby_pipe[2];
    if (pipe(lobby_pipe) == -1) {
        LOG_ERROR("Errore nella creazione della pipe della lobby");
        exit(EXIT_FAILURE);
    }

    pthread_t lobby_thread_id;
    if (pthread_create(&lobby_thread_id, NULL, lobby_thread_main, &lobby_pipe[0]) != 0) {
        LOG_ERROR("Errore durante la creazione del thread della lobby");
        exit(EXIT_FAILURE);
    }

    LOG_INFO("Server in attesa di connessioni...");

    int conn_s;
    struct sockaddr_in their_addr;
    socklen_t sin_size = sizeof(struct sockaddr_in);

    // Accetta le connessioni in un ciclo infinito
    // e passa il file descriptor della connessione al thread della lobby
    // per gestire la comunicazione con il client.
    while (1) {
        if ((conn_s = accept(list_s, (struct sockaddr*)&their_addr, &sin_size)) < 0) {
            LOG_ERROR("Errore durante l'accept");
            continue; // Continua ad accettare altre connessioni
        }

        LOG_INFO("Connessione da %s", inet_ntoa(their_addr.sin_addr));
        // Passa il nuovo file descriptor al thread lobby scrivendo sulla pipe
        if (write(lobby_pipe[1], &conn_s, sizeof(conn_s)) == -1) {
            LOG_ERROR("Errore durante la scrittura sulla pipe della lobby");
        }
    }


}

