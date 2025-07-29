#include "cmdLineParser.h"
#include <stdlib.h>
#include <stdio.h>
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
    int i = 1;
    while(i < argc){
        int foundMatch = 0;
        if(argv[i][0] == '-'){
            ArgvParam *curr = argvParams;
            while(curr->next != NULL){
                curr = curr->next;
                if(!strcmp(argv[i]+1, curr->paramName)){
                    if(curr->isSet){
                        printf("Al parametro -%s è già stato assegnato un valore\n", curr->paramName);
                        printUsage(argv[0], argvParams);
                        exit(0);
                    }
                    foundMatch = 1;
                    curr->isSet = 1;
                    curr->paramValue = NULL;

                    if(i+1 < argc && argv[i+1][0] != '-'){
                        curr->paramValue = argv[i+1];
                        i++;
                    } else if(curr->isValueRequired && (i == argc -1 || (i+1 < argc && argv[i+1][0] == '-'))){
                        printf("Il parametro -%s richiede un valore\n", curr->paramName);
                        printUsage(argv[0], argvParams);
                        exit(0);
                    }
                    break;
                }
            }
        }
        if(!foundMatch){
            printf("L'argomento %s non corrisponde ad alcune nome di parametro\n", argv[i]);
            printUsage(argv[0], argvParams);
            exit(0);
        }
        i++;
    }

    ArgvParam *curr = argvParams;
    while(curr->next != NULL){
        curr = curr->next;

        if(curr->isParamRequired && curr->isSet == 0){
            printf("Non è stato fornito un valore per -%s\n", curr->paramName);
            printUsage(argv[0], argvParams);
            exit(0);
        }

    }
}

void printUsage(char *fileName, ArgvParam *argvParams){
    printf("Usage: %s", fileName);
    ArgvParam *curr = argvParams;
    while(curr->next != NULL){
        curr = curr->next;

        printf((curr->isParamRequired) ? " -%s" : " [-%s", curr->paramName);
        printf((curr->isValueRequired) ? " value" : "");
        printf((curr->isParamRequired) ? "" : "]");
    }

    printf("\n");
}

/**
 * Crea una lista collegata di strutture ArgvParam a partire da
 * una stringa di nomi di parametri separati da virgola.
 * 
 * @param paramsName Stringa nel formato "RVparamName1,-VparamName2,R-NparamName3,..",
 *              R = param is required, V = a value for the param is required, - = nothing
 * @return Puntatore alla testa della lista ArgvParam (il primo elemento utile è head->next).
 */
ArgvParam *setArgvParams(char *paramsName){
    ArgvParam *head = (ArgvParam *)calloc(1, sizeof(ArgvParam));
    ArgvParam *curr = head;

    char *paramsCopy = strdup(paramsName);
    char *token = strtok(paramsCopy, ",");

    while(token != NULL){
        ArgvParam *new = (ArgvParam *)malloc(sizeof(ArgvParam));
        new->paramName = strdup(token+2);
        new->isParamRequired = (token[0] == 'R') ? 1 : 0;
        new->isValueRequired = (token[1] == 'V') ? 1 : 0;
        new->next = NULL;

        curr->next = new;
        curr = new;
        token = strtok(NULL, ",");
    }

    free(paramsCopy);
    return head;
}

/**
 * @param paramName Nome del parametro.
 * @param argvParams Lista concatenata di ArgvParam.
 * @return Valore del parametro se questo è stato impostato (char *); NULL altrimenti.
 */
char *getArgvParamValue(char *paramName, ArgvParam *argvParams){
    ArgvParam *curr = argvParams;
    while(curr->next != NULL){
        curr = curr->next;
        if(!strcmp(curr->paramName, paramName)){
            if(curr->isSet){
                return curr->paramValue;
            } else break;
        }
    }
    return NULL;
}

/**
 * Libera la memoria allocata per la lista di ArgvParam.
 * @param head Puntatore alla testa della lista di ArgvParam.
 */
void freeArgvParams(ArgvParam *head){
    ArgvParam *curr = head;
    while(curr){
        ArgvParam *next = curr->next;
        free(curr->paramName);
        free(curr->paramValue);
        free(curr);
        curr = next;
    }
}