#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>

#include "utils/userInput.h"
#include "utils/debug.h"


/**
 * Converte una stringa in un intero, gestendo errori di conversione e limiti.
 * @param str La stringa da convertire.
 * @param value Puntatore a un intero dove memorizzare il risultato.
 * @return 0 se la conversione è riuscita, -1 in caso di errore.
 */
int getIntFromString(char *str, int *value) {
    char *endPtr;
    long tempValue = strtol(str, &endPtr, 10);
    if (*endPtr != '\0' || tempValue < INT_MIN || tempValue > INT_MAX) {
        // La stringa non è un numero valido o fuori dai limiti di int
        LOG_ERROR("Errore: '%s' non è un numero valido o fuori dai limiti di int", str);
        return -1;
    }
    *value = (int)tempValue;
    return 0;
}

/**
 * Converte un intero in una stringa allocata dinamicamente.
 * La stringa deve essere liberata dal chiamante con free().
 * @param value L'intero da convertire.
 * @return Puntatore alla stringa contenente la rappresentazione dell'intero, o NULL in caso di errore.
 */
char *getStringFromInt(int value){
    char *str = (char *)malloc(12 * sizeof(char));
    if (str == NULL) {
        LOG_ERROR("Memory allocation failed for string conversion");
        return NULL;
    }
    snprintf(str, 12, "%d", value);
    return str;
}


/**
 * Legge una stringa alfanumerica da input standard, limitando la lunghezza a maxLength.
 * La stringa restituita va liberata dal chiamante con free().
 * @param maxLength Lunghezza massima della stringa da leggere.
 * @return Puntatore alla stringa letta, o NULL in caso di errore.
 */
char *readAlfanumericString(int maxLength) {
    // Aggiungiamo +2: +1 per un eventuale '\n' e +1 per il terminatore '\0'
    char *input = malloc(maxLength + 2);
    if (!input) {
        LOG_ERROR("Errore di allocazione memoria per il buffer di input");
        return NULL;
    }

read:
    if (fgets(input, maxLength + 2, stdin) == NULL) {
        LOG_ERROR("Errore durante la lettura dell'input");
        free(input);
        return NULL; // Errore di lettura o fine del file
    }
    eof_handler();

    // Cerca il carattere di newline. Se non c'è, l'input era troppo lungo
    char *newline = strchr(input, '\n');
    if (newline == NULL) {
        // L'input è stato troncato
        LOG_WARNING("Nome partita troppo lungo, verrà troncato ai primi %d caratteri", maxLength);
        input[maxLength] = '\0'; // Troncamento
        // Puliamo il resto del buffer di stdin
        int c;
        while ((c = getchar()) != '\n' && c != EOF) {}
    } else {
        // Trovato il newline, lo sostituiamo con il terminatore nullo.
        *newline = '\0';
    }

    // Controllo se tutti i caratteri sono tra quelli permessi
    for (int i = 0; input[i] != '\0'; i++) {
        if (!((input[i] >= 'a' && input[i] <= 'z') ||
              (input[i] >= 'A' && input[i] <= 'Z') ||
              (input[i] >= '0' && input[i] <= '9') ||
              input[i] == ' ' || input[i] == '-' || input[i] == '_' || input[i] == '.')) {
            LOG_ERROR("Carattere non permesso: '%c' in '%s'", input[i], input);
            printf("Sono ammessi solo lettere, numeri, spazi, '.', '-' e '_'.\nReinserisci: ");

            goto read;
        }
    }

    return input;
}

void eof_handler() {
    if(feof(stdin)) {
        printf("\nChiusura programma (EOF ricevuto).\n");
        exit(0);
    }
}