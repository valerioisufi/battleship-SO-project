#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include "common/protocol.h"


/**
 * Invia un flusso di byte su una socket, assicurandosi che tutti i byte richiesti vengano trasmessi.
 * 
 * @param socket_fd File descriptor della socket su cui inviare.
 * @param buffer Puntatore al buffer contenente i dati da inviare.
 * @param num_bytes Numero totale di byte da inviare.
 * @return 0 se tutti i byte sono stati inviati correttamente, -1 in caso di errore o disconnessione.
 */
int sendByteStream(int socket_fd, char *buffer, size_t num_bytes){
    size_t bytes_sent = 0;
    int result;

    while(bytes_sent < num_bytes){
        result = send(socket_fd, buffer + bytes_sent, num_bytes - bytes_sent, 0);
        if(result < 0){
            if(errno == EINTR) continue;
            // Errore o disconnessione
            close(socket_fd);
            return -1;
        }
        bytes_sent += result;
    }
    return 0;
}

/**
 * Riceve un flusso di byte da una socket, assicurandosi che tutti i byte richiesti vengano letti.
 * 
 * @param socket_fd File descriptor della socket da cui ricevere.
 * @param buffer Puntatore al buffer dove scrivere i dati ricevuti.
 * @param num_bytes Numero totale di byte da ricevere.
 * @return 0 se tutti i byte sono stati ricevuti correttamente, -1 in caso di errore o disconnessione.
 */
int recvByteStream(int socket_fd, char *buffer, size_t num_bytes){
    size_t bytes_received = 0;
    int result;

    while(bytes_received < num_bytes){
        result = recv(socket_fd, buffer + bytes_received, num_bytes - bytes_received, 0);
        if(result <= 0){
            if(errno == EINTR) continue;
            // Errore o disconnessione
            close(socket_fd);
            return -1;
        }

        bytes_received += result;
    }
    return 0;
}

/**
 * Legge un messaggio da socket nella sua interezza.
 * @param sock_fd File descriptor della socket da cui leggere.
 * @return Puntatore a struttura Msg contenente header e payload ricevuti.
 * 
 */
Msg *recvMsg(int socket_fd){
    char header_buffer[HEADER_SIZE];
    if(recvByteStream(socket_fd, header_buffer, HEADER_SIZE) == -1){
        return NULL;
    }

    Header header;
    memcpy(&header, header_buffer, HEADER_SIZE);
    uint32_t payload_size = header.payloadSize;

    char *payload_buffer = (char *)malloc(payload_size + 1);
    if(payload_buffer == NULL) exit(EXIT_FAILURE);
    if(recvByteStream(socket_fd, payload_buffer, payload_size) == -1){
        free(payload_buffer);
        return NULL;
    }
    payload_buffer[payload_size] = '\0'; // Assicura che il payload sia una stringa C valida

    // a questo punto dispongo del messaggio completo
    Msg *msg = (Msg *)malloc(sizeof(Msg));
    if(msg == NULL) exit(EXIT_FAILURE);

    msg->header = header;
    msg->payload = payload_buffer;

    return msg;
}

/**
 * Invia un messaggio completo (header e payload) su una socket.
 * 
 * @param sock_fd File descriptor della socket su cui inviare.
 * @param msg Puntatore alla struttura Msg contenente header e payload da inviare.
 * @return 0 se il messaggio è stato inviato correttamente, -1 in caso di errore o disconnessione.
 *
 * La funzione invia prima l'header e poi il payload, assicurandosi che tutti i byte vengano trasmessi.
 */
int sendMsg(int socket_fd, Msg *msg){
    char *header_buffer = (char *)&msg->header;

    if(sendByteStream(socket_fd, header_buffer, HEADER_SIZE) == -1){
        return -1;
    }

    if(sendByteStream(socket_fd, msg->payload, msg->header.payloadSize) == -1){
        return -1;
    }
    return 0;
}



/**
 * Crea una nuova struttura Msg, allocando memoria e copiando il payload.
 *
 * @param header_type Tipo del messaggio (campo msgType dell'header).
 * @param payload_size Dimensione del payload in byte.
 * @param payload Puntatore alla stringa del payload da copiare.
 * @return Puntatore alla struttura Msg allocata dinamicamente.
 */
Msg *createMsg(uint16_t header_type, uint32_t payload_size, char *payload){
    Msg *msg = (Msg *)malloc(sizeof(Msg));
    if(msg == NULL) exit(EXIT_FAILURE);

    msg->header.msgType = header_type;
    msg->header.payloadSize = payload_size;
    msg->payload = strdup(payload);

    return msg;
}

/**
 * Libera la memoria allocata per una struttura Msg, incluso il payload.
 * @param msg Puntatore alla struttura Msg da liberare.
 */
void freeMsg(Msg *msg){
    free(msg->payload);
    free(msg);
}

/**
 *  Restituisce una nuova stringa in cui tutti i caratteri speciali ('|', ':', '\\')
 *  presenti nella stringa di input vengono preceduti da un carattere di escape '\\'.
 *  Utile per serializzare stringhe che devono essere trasmesse in protocolli testuali
 *  dove questi caratteri hanno un significato particolare.
 *  La memoria restituita va liberata dal chiamante con free().
 *
 * @param src Stringa di input da processare.
 * @return Puntatore a nuova stringa allocata dinamicamente.
 */
char *escapeString(char *src){
    int len = strlen(src);
    int count = 0;
    for(int i = 0; i < len; i++){
        if(src[i] == '|' || src[i] == ':' || src[i] == '\\') count++;
    }

    char *dst = (char *)malloc(len + count + 1);
    if(dst == NULL) exit(EXIT_FAILURE);

    int i = 0, j = 0;
    while(i < len){
        if(src[i] == '|' || src[i] == ':' || src[i] == '\\'){
            dst[j++] = '\\'; // aggiungi il backslash
            dst[j++] = src[i++] ^ 0x7f;
        } else {
            dst[j++] = src[i++];
        }
        
    }

    dst[j] = '\0';
    return dst;
}

/**
 *  Restituisce una nuova stringa in cui tutte le sequenze di escape '\\' vengono rimosse,
 *  ripristinando i caratteri speciali originali. È l'operazione inversa di escapeString.
 *  La memoria restituita va liberata dal chiamante con free().
 *
 * @param src Stringa di input da processare.
 * @return Puntatore a nuova stringa allocata dinamicamente.
 */
char *unescapeString(char *src){
    int len = strlen(src);
    char *dst = (char *)malloc(len+1);
    if(dst == NULL) exit(EXIT_FAILURE);

    int i = 0, j = 0;
    while(i < len){
        if(src[i] == '\\'){
            i++; // salta il backslash
            dst[j++] = src[i++] ^ 0x7f;
        } else{
            dst[j++] = src[i++];
        }
    }

    dst[j] = '\0';
    return dst;
}

/**
 * Parsa una stringa di input formattata come "key1:value1|key2:value2|..."
 * in una lista concatenata di PayloadNode, dove ogni nodo contiene una coppia chiave-valore.
 * La memoria allocata per la lista va liberata con freePayloadNodes.
 * @param buffer Stringa di input da processare.
 * @return Puntatore alla testa della lista di PayloadNode.
 */
PayloadNode *parsePayload(char *buffer){
    char *buf_copy = strdup(buffer);
    if (!buf_copy) exit(EXIT_FAILURE);

    PayloadNode *head = (PayloadNode *)calloc(1, sizeof(PayloadNode));
    if(head == NULL) exit(EXIT_FAILURE);
    PayloadNode *curr = head;

    char *pair = strtok(buf_copy, "|");
    while(pair){
        char *sep = strchr(pair, ':');
        if(!sep){
            pair = strtok(NULL, "|");
            continue; // formato errato, salta
        }
        *sep = '\0';

        char *key_escaped = pair;
        char *value_escaped = sep + 1;

        char *key = unescapeString(key_escaped);
        char *value = unescapeString(value_escaped);

        PayloadNode *new = (PayloadNode *)malloc(sizeof(PayloadNode));
        if(new == NULL) exit(EXIT_FAILURE);
        new->key = key;
        new->value = value;
        new->next = NULL;

        curr->next = new;
        curr = new;

        pair = strtok(NULL, "|");
    }

    free(buf_copy);
    return head;
}

/**
 * Serializza una lista di PayloadNode in una stringa formattata come "key1:value1|key2:value2|...".
 * La stringa restituita va liberata dal chiamante con free().
 * @param head Puntatore alla testa della lista di PayloadNode.
 * @return Puntatore a stringa allocata dinamicamente con la serializzazione.
 */
char *serializePayload(PayloadNode *head){
    if(head == NULL) return strdup(""); // caso base, lista vuota

    size_t total_size = 0;
    PayloadNode *curr = head->next; // salta il nodo sentinella
    while(curr){
        total_size += strlen(curr->key) + strlen(curr->value) + 2; // +2 per ':' e '|'
        curr = curr->next;
    }

    char *buffer = (char *)malloc(total_size + 1);
    if(buffer == NULL) exit(EXIT_FAILURE);

    char *ptr = buffer;
    curr = head->next; // salta il nodo sentinella
    while(curr){
        ptr += sprintf(ptr, "%s:%s|", escapeString(curr->key), escapeString(curr->value));
        curr = curr->next;
    }

    // rimuovo l'ultimo '|'
    if(ptr > buffer) ptr--; 
    *ptr = '\0';

    return buffer;
}

/**
 * Aggiorna il valore associato a una chiave nella lista di PayloadNode.
 * Se la chiave non esiste, aggiunge un nuovo nodo in fondo alla lista.
 * Se il nodo esiste, libera il vecchio valore e lo sostituisce con il nuovo.
 * Se la lista è vuota, crea un nodo head.
 * @param head Puntatore al nodo sentinella della lista di PayloadNode.
 * @param key Chiave da aggiornare o inserire.
 * @param value Nuovo valore da associare alla chiave.
 * @return Puntatore alla testa della lista aggiornata.
 */
PayloadNode *updatePayload(PayloadNode *head, char *key, char *value){
    if(key == NULL || value == NULL) {
        fprintf(stderr, "updatePayload: key or value is NULL\n");
        exit(EXIT_FAILURE);
    }
    if(head == NULL) {
        head = (PayloadNode *)calloc(1, sizeof(PayloadNode));
    }
    PayloadNode *curr = head;
    while(curr->next){
        curr = curr->next;

        if(strcmp(curr->key, key) == 0){
            free(curr->value); // libera il vecchio valore
            curr->value = strdup(value); // assegna il nuovo valore
            return head;
        }
    }

    // la chiave non esiste, la aggiungo alla fine
    PayloadNode *new = (PayloadNode *)malloc(sizeof(PayloadNode));
    if(new == NULL) exit(EXIT_FAILURE);

    new->key = strdup(key);
    new->value = strdup(value);
    new->next = NULL;

    curr->next = new; // aggiungo il nuovo nodo alla fine della lista
    return head;
}

/**
 * Restituisce una copia allocata dinamicamente del valore associato a una chiave nella lista di PayloadNode.
 * La stringa restituita va liberata dal chiamante con free().
 * @param head Puntatore alla testa della lista di PayloadNode.
 * @param key Chiave da cercare.
 * @return Puntatore a una nuova stringa allocata dinamicamente con il valore associato, oppure NULL se la chiave non esiste.
 */
char *getPayloadValue(PayloadNode *head, char *key){
    PayloadNode *curr = head;
    while(curr->next){
        curr = curr->next;

        if(strcmp(curr->key, key) == 0){
            return strdup(curr->value); // trovato, restituisco il valore
        }
    }

    return NULL; // chiave non trovata
}

/**
 * Libera la memoria allocata dinamicamente per la lista concatenata di PayloadNode.
 * Ogni nodo viene liberato, inclusi i campi key e value.
 * @param head Puntatore alla testa della lista di PayloadNode.
 */
void freePayloadNodes(PayloadNode *head){
    PayloadNode *curr = head;
    while(curr){
        PayloadNode *next = curr->next;
        free(curr->key);
        free(curr->value);
        free(curr);
        curr = next;
    }
}



/**
 * Invia un messaggio a un client in modo sicuro, gestendo errori e cleanup automatico.
 * Se l'invio fallisce,  libera le risorse.
 * @param client_fd File descriptor della socket del client.
 * @param msg_type Tipo del messaggio da inviare.
 * @param payload Lista di PayloadNode da serializzare e inviare come payload.
 * @return 0 se l'invio è andato a buon fine, -1 in caso di errore (con cleanup già effettuato).
 */
int safeSendMsg(int client_fd, uint16_t msg_type, PayloadNode *payload){
    char *serialized = serializePayload(payload);
    Msg *msg = createMsg(msg_type, strlen(serialized) + 1, serialized);
    if (sendMsg(client_fd, msg) == -1) {
        freeMsg(msg);
        free(serialized);
        freePayloadNodes(payload);
        return -1;
    }

    freeMsg(msg);
    free(serialized);
    freePayloadNodes(payload);
    return 0;
}


/**
 * Riceve un messaggio da un client in modo sicuro, gestendo errori e cleanup automatico.
 * In caso di errore o disconnessione, libera le risorse.
 * @param client_fd File descriptor della socket del client.
 * @param msg_type_out Puntatore dove verrà scritto il tipo del messaggio ricevuto.
 * @param payload_out Puntatore dove verrà scritto il payload deserializzato (lista di PayloadNode).
 * @return 0 se la ricezione è andata a buon fine, -1 in caso di errore (con cleanup già effettuato).
 */
int safeRecvMsg(int client_fd, uint16_t *msg_type_out, PayloadNode **payload_out) {
    Msg *received_msg = recvMsg(client_fd);
    if (received_msg == NULL) {
        return -1;
    }

    *msg_type_out = received_msg->header.msgType;
    *payload_out = parsePayload(received_msg->payload);
    freeMsg(received_msg);
    return 0;
}