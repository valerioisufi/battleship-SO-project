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

#include "utils/cmdLineParser.h"
#include "common/protocol.h"

int main(int argc, char *argv[]){
    signal(SIGPIPE, SIG_IGN);

    ArgvParam *allowedArgs = setArgvParams("RVaddress,RVport");
    parseCmdLine(argc, argv, allowedArgs);

    // ArgvParam *curr = allowedArgs;
    // while(curr->next){
    //     curr = curr->next;
    //     printf("%s %d %d (%s)\n", curr->paramName, curr->isParamRequired, curr->isValueRequired, curr->paramValue);
    // }

    char *addressString = getArgvParamValue("address", allowedArgs);
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
    servaddr.sin_port = htons(port);

    struct hostent *he = NULL;
    if(inet_aton(addressString, &servaddr.sin_addr) <= 0){

        if((he = gethostbyname(addressString)) == NULL){
            printf("Indirizzo IP non valido, risoluzione nome fallita\n");
            exit(EXIT_FAILURE);
        }

        servaddr.sin_addr = *((struct in_addr *)he->h_addr_list[0]);
    }

    int conn_s; // connection socket
    if((conn_s = socket(AF_INET, SOCK_STREAM, 0)) < 0){
        printf("Errore durante la creazione della socket");
        exit(EXIT_FAILURE);
    }
    
    if(connect(conn_s, (struct sockaddr *) &servaddr, sizeof(servaddr)) < 0){
        printf("Errore durante la connect\n");
        exit(EXIT_FAILURE);
    }


    // richiedo il nome dell'utente
    char user[31];
    printf("Inserire un nome utente (max 30 caratteri): ");
    fgets(user, sizeof(user), stdin);
    user[strcspn(user, "\n")] = 0; // rimuovo il newline

    if(strlen(user) >= 30){
        printf("Nome utente troppo lungo, verrà troncato ai primi 30 caratteri");
        user[30] = 0;
    }


    // creo il messaggio di login
    PayloadNode *payload = (PayloadNode *)updatePayload(NULL, "username", user);
    Msg *msg = createMsg(MSG_LOGIN, HEADER_SIZE + strlen(user) + 1, serializePayload(payload));
    sendMsg(conn_s, msg);

    freeMsg(msg);
    freePayloadNodes(payload);


    Msg *received_msg = recvMsg(conn_s); // Gestisce il messaggio ricevuto dal client
    payload = parsePayload(received_msg->payload);

    switch(received_msg->header.msgType){
        case MSG_WELCOME:
            printf("Benvenuto nel gioco!\n");
            break;

        default:
            fprintf(stderr, "Messaggio non riconosciuto: %d\n", received_msg->header.msgType);
            break;
    }

    freeMsg(received_msg);
    freePayloadNodes(payload);

    pause();

    close(conn_s);
    freeArgvParams(allowedArgs);
    return 0;

}
