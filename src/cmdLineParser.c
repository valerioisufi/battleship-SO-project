#include "cmdLineParser.h"
#include <stdlib.h>
#include <string.h>

/**
 * Effettua il parsing degli argomenti della riga di comando.
 * Analizza i parametri passati a main e aggiorna l'array di struct ArgvPara.
 * 
 * Il chiamante imposta ArgvParam.paramName e ArgvParam.paramShortName; questi valori
 * sono poi utilizzati per individuare negli argomenti passati da riga di comando
 * sequenze del tipo "-paramName paramValue" oppure "-paramShortName paramValue"
 * 
 * @param argc Numero di parametri passati da riga di comando.
 * @param argv Array di stringhe contenente gli argomenti.
 * @param argvParams Puntatore a un array di ArgvParam da aggiornare.
 * @return Nessun valore di ritorno. Modifica argvParams tramite il puntatore.
 */
void parseCmdLine(int argc, char *argv[], ArgvParam *argvParams){
    int i = 0;
    while(i < argc){
        if(argv[i][0] == '-'){
            ArgvParam *curr = argvParams;
            while(curr->next != NULL){
                curr = curr->next;
                if(!strcmp(argv[i]+1, curr->paramName)){
                    curr->isSet = 1;
                    curr->paramValue = NULL;

                    if(i+1 < argc && argv[i+1][0] != '-'){
                        curr->paramValue = argv[i+1];
                        i++;
                    }
                    break;
                }
            }
        }
        i++;
    }

}

/**
 * Crea una lista collegata di strutture ArgvParam a partire da
 * una stringa di nomi di parametri separati da virgola.
 * 
 * @param paramsName Stringa nel formato "paramName1,paramName2,paramName3,..".
 * @return Puntatore alla testa della lista ArgvParam (il primo elemento utile è head->next).
 */
ArgvParam *setArgvParams(char *paramsName){
    ArgvParam *head = (ArgvParam *)calloc(1, sizeof(ArgvParam));
    ArgvParam *curr = head;

    char *token = strtok(paramsName, ",");
    while(token != NULL){
        ArgvParam *new = (ArgvParam *)malloc(sizeof(ArgvParam));
        new->paramName = strdup(token);
        new->next = NULL;

        curr->next = new;
        token = strtok(NULL, ",");
    }

    return head;
}