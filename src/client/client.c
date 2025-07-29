#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <sys/socket.h>     // socket definitions
#include <sys/types.h>      // socket types
#include <arpa/inet.h>      // inet (3) functions
#include <unistd.h>
#include <netinet/in.h>
#include <netdb.h>

#include "utils/cmdLineParser.h"

int main(int argc, char *argv[]){
    ArgvParam *allowedArgs = setArgvParams("RVaddress,RVport");
    parseCmdLine(argc, argv, allowedArgs);

    // ArgvParam *curr = allowedArgs;
    // while(curr->next){
    //     curr = curr->next;
    //     printf("%s %d %d (%s)\n", curr->paramName, curr->isParamRequired, curr->isValueRequired, curr->paramValue);
    // }

    char *address = getArgvParamValue("address", allowedArgs);
    char *port = getArgvParamValue("port", allowedArgs);

    
    struct sockaddr_in servaddr;
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;

    char *endPtr;
    servaddr.sin_port = htons(strtol(port, &endPtr, 0));
    if(*endPtr){
        printf("Porta non riconosciuta\n");
        exit(EXIT_FAILURE);
    }

    struct hostent *he = NULL;
    if(inet_aton(address, &servaddr.sin_addr) <= 0){

        if((he = gethostbyname(address)) == NULL){
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

    




}
