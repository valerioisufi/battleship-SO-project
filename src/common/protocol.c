#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

#include <sys/socket.h>
#include <sys/epoll.h>
#include "common/protocol.h"
#include "utils/userInput.h"


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
            return -1;
        }

        bytes_received += result;
    }
    return 0;
}

/**
 * Legge un messaggio da socket nella sua interezza.
 * @param sock_fd File descriptor della socket da cui leggere.
 * @return Puntatore a struttura Msg contenente header e payload ricevuti, o NULL in caso di errore.
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
    if(payload_buffer == NULL) {
        return NULL; // Errore di allocazione
    }
    if(recvByteStream(socket_fd, payload_buffer, payload_size) == -1){
        free(payload_buffer);
        return NULL;
    }
    payload_buffer[payload_size] = '\0'; // Assicura che il payload sia una stringa C valida

    // a questo punto dispongo del messaggio completo
    Msg *msg = (Msg *)malloc(sizeof(Msg));
    if(msg == NULL) {
        return NULL; // Errore di allocazione
    }

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
 * @return Puntatore alla struttura Msg allocata dinamicamente, o NULL in caso di errore.
 */
Msg *createMsg(uint16_t header_type, uint32_t payload_size, char *payload){
    Msg *msg = (Msg *)malloc(sizeof(Msg));
    if(msg == NULL) {
        return NULL; // Errore di allocazione
    }

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
 *  Restituisce una nuova stringa in cui tutti i caratteri speciali ('|', ':', '[', ']', '\\')
 *  presenti nella stringa di input vengono preceduti da un carattere di escape '\\'.
 *  Utile per serializzare stringhe che devono essere trasmesse in protocolli testuali
 *  dove questi caratteri hanno un significato particolare.
 *  La memoria restituita va liberata dal chiamante con free().
 *
 * @param src Stringa di input da processare.
 * @return Puntatore a nuova stringa allocata dinamicamente, o NULL in caso di errore.
 */
char *escapeString(const char *src){
    int len = strlen(src);
    int count = 0;
    for(int i = 0; i < len; i++){
        if(src[i] == '|' || src[i] == ':' || src[i] == '[' || src[i] == ']' || src[i] == ',' || src[i] == '\\') count++;
    }

    char *dst = (char *)malloc(len + count + 1);
    if(dst == NULL) {
        return NULL; // Errore di allocazione
    }

    int i = 0, j = 0;
    while(i < len){
        if(src[i] == '|' || src[i] == ':' || src[i] == '[' || src[i] == ']' || src[i] == ',' || src[i] == '\\'){
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
 * @return Puntatore a nuova stringa allocata dinamicamente, o NULL in caso di errore.
 */
char *unescapeString(const char *src){
    int len = strlen(src);
    char *dst = (char *)malloc(len+1);
    if(dst == NULL) {
        return NULL; // Errore di allocazione
    }

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
 * Crea un nuovo Payload vuoto.
 * @return Un puntatore a un nuovo Payload, o NULL in caso di errore.
 */
Payload *createEmptyPayload() {
    Payload *payload = calloc(1, sizeof(Payload));
    return payload;
}

/**
 * Aggiunge una coppia chiave-valore all'ultima lista del Payload.
 * Se il Payload non ha liste, ne crea una nuova.
 * @param payload Il Payload da modificare.
 * @param key La chiave da aggiungere.
 * @param value Il valore da aggiungere.
 * @return 0 in caso di successo, -1 in caso di errore.
 */
int addPayloadKeyValuePair(Payload *payload, const char *key, const char *value) {
    if (!payload || !key || !value) return -1;

    // Se il payload è vuoto, aggiungi la prima lista
    if (payload->tail == NULL) {
        if (addPayloadList(payload) != 0) return -1;
    }

    // Crea il nuovo nodo
    PayloadNode *newNode = malloc(sizeof(PayloadNode));
    if (!newNode) return -1;
    newNode->key = strdup(key);
    newNode->value = strdup(value);
    newNode->next = NULL;

    if (!newNode->key || !newNode->value) {
        free(newNode->key);
        free(newNode->value);
        free(newNode);
        return -1;
    }

    // Aggiungi il nodo alla fine della lista corrente (nell'ultima PayloadList)
    PayloadNode *current = payload->tail->head;
    if (current == NULL) {
        payload->tail->head = newNode;
    } else {
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = newNode;
    }
    
    return 0;
}

int addPayloadKeyValuePairInt(Payload *payload, const char *key, int value) {
    if (!payload || !key) return -1;

    char value_str[12];
    snprintf(value_str, sizeof(value_str), "%d", value);
    
    return addPayloadKeyValuePair(payload, key, value_str);
}


/**
 * Aggiunge una nuova lista vuota alla fine del Payload.
 * @param payload Il Payload a cui aggiungere la lista.
 * @return 0 in caso di successo, -1 in caso di errore.
 */
int addPayloadList(Payload *payload) {
    if (!payload) return -1;

    PayloadList *newList = calloc(1, sizeof(PayloadList));
    if (!newList) return -1;

    if (payload->tail == NULL) {
        payload->head = payload->tail = newList;
    } else {
        payload->tail->next = newList;
        payload->tail = newList;
    }
    payload->size++;
    return 0;
}

 /**
 * Restituisce il valore associato a una chiave in una specifica lista del Payload.
 * @param payload Il Payload in cui cercare.
 * @param index L'indice della lista (0-based) in cui cercare.
 * @param key La chiave da trovare.
 * @return Una copia del valore se trovato, altrimenti NULL. Il chiamante deve liberare la memoria.
 */
char *getPayloadValue(Payload *payload, int index, const char *key) {
    if (!payload || !payload->head || index < 0 || index >= payload->size) {
        return NULL;
    }

    PayloadList *current_list = payload->head;
    for (int i = 0; i < index; i++) {
        current_list = current_list->next;
    }

    PayloadNode *current_node = current_list->head;
    while (current_node) {
        if (strcmp(current_node->key, key) == 0) {
            return strdup(current_node->value);
        }
        current_node = current_node->next;
    }

    return NULL; // Chiave non trovata in quella lista
}

/**
 * Restituisce il valore intero associato a una chiave in una specifica lista del Payload.
 * @param payload Il Payload in cui cercare.
 * @param index L'indice della lista (0-based) in cui cercare.
 * @param key La chiave da trovare.
 * @param value_out Puntatore a un intero dove verrà memorizzato il valore trovato.
 * @return 0 se il valore è stato trovato e convertito correttamente, -1 altrimenti.
 */
int getPayloadIntValue(Payload *payload, int index, const char *key, int *value_out) {
    if (!payload || !key || !value_out) return -1;

    char *value_str = getPayloadValue(payload, index, key);
    if (!value_str) return -1;

    int ret = getIntFromString(value_str, value_out);
    free(value_str);
    return ret;
}

/**
 * Restituisce la dimensione della lista di PayloadNode in una specifica lista del Payload.
 * @param payload Il Payload in cui cercare.
 * @return Il numero di nodi nella lista, o 0 se il Payload è NULL o vuoto.
 */
int getPayloadListSize(Payload *payload) {
    if (!payload) return 0;
    return payload->size;
}


/**
 * Parsa una stringa "key1:value1|key2:value2" in una lista di PayloadNode.
 * Helper interno per parsePayload.
 * @param list_str La stringa che rappresenta una singola lista di chiavi e valori.
 * @param target_list Puntatore dove verrà memorizzata la testa della lista di nodi creata.
 * @return 0 in caso di successo, -1 in caso di errore.
 */
static int parsePayloadListFromString(char *list_str, PayloadList *target_list) {
    char *pair_saveptr;
    char *pair = strtok_r(list_str, "|", &pair_saveptr);
    PayloadNode *current_node = NULL;

    while (pair) {
        char *sep = strchr(pair, ':');
        if (!sep) {
            pair = strtok_r(NULL, "|", &pair_saveptr);
            continue; // Formato non valido, salta
        }
        *sep = '\0';

        char *key_unescaped = unescapeString(pair);
        char *value_unescaped = unescapeString(sep + 1);
        if (!key_unescaped || !value_unescaped) {
             free(key_unescaped);
             free(value_unescaped);
             return -1; // Errore di unescape
        }

        PayloadNode *newNode = malloc(sizeof(PayloadNode));
        if (!newNode) {
            free(key_unescaped);
            free(value_unescaped);
            return -1;
        }
        newNode->key = key_unescaped;
        newNode->value = value_unescaped;
        newNode->next = NULL;

        // Aggiungi il nodo alla lista
        if (target_list->head == NULL) {
            target_list->head = newNode;
            current_node = newNode;
        } else {
            current_node->next = newNode;
            current_node = newNode;
        }

        pair = strtok_r(NULL, "|", &pair_saveptr);
    }
    return 0;
}

/**
 * Parsa un buffer serializzato in una struttura Payload.
 * Formato atteso: "[k1:v1|k2:v2],[k3:v3|k4:v4]"
 * @param buffer La stringa serializzata.
 * @return Un puntatore a un nuovo Payload, o NULL in caso di errore.
 */
Payload *parsePayload(char *buffer) {
    if (!buffer) return NULL;

    char *buf_copy = strdup(buffer);
    if (!buf_copy) return NULL;

    Payload *payload = createEmptyPayload();
    if (!payload) {
        free(buf_copy);
        return NULL;
    }

    // strtok non gestisce bene i token vuoti, quindi usiamo un approccio manuale
    char *cursor = buf_copy;
    while (*cursor) {
        // Salta le virgole iniziali
        if (*cursor == ',') cursor++;

        if (*cursor == '\0') break;

        // Trova l'inizio e la fine di una lista `[...]`
        char* start = strchr(cursor, '[');
        if (!start) break;
        char* end = strchr(start, ']');
        if (!end) break; // Malformato

        *end = '\0'; // Termina la stringa della lista
        char *list_content = start + 1;

        // Aggiungi una nuova PayloadList al payload
        if (addPayloadList(payload) != 0) {
            freePayload(payload);
            free(buf_copy);
            return NULL;
        }

        // Parsa il contenuto della lista e popola l'ultima PayloadList aggiunta
        if (parsePayloadListFromString(list_content, payload->tail) != 0) {
            freePayload(payload);
            free(buf_copy);
            return NULL;
        }
        
        cursor = end + 1;
    }

    free(buf_copy);
    return payload;
}


/**
 * Serializza una struttura Payload in una stringa.
 * Formato di output: "[k1:v1|k2:v2],[k3:v3]"
 * @param payload La struttura Payload da serializzare.
 * @return Una stringa allocata dinamicamente, o NULL in caso di errore.
 */
char *serializePayload(Payload *payload) {
    if (!payload || !payload->head) {
        return strdup(""); // Payload vuoto o non inizializzato
    }

    size_t total_size = 0;
    PayloadList *current_list = payload->head;

    // Calcola la dimensione totale necessaria, effettuando l'escape
    while (current_list) {
        total_size += 3; // Per '[', ']' e la virgola di separazione
        PayloadNode *current_node = current_list->head;

        while (current_node) {
            char *key_esc = escapeString(current_node->key);
            char *val_esc = escapeString(current_node->value);

            if (!key_esc || !val_esc) { // Gestione errore
                 free(key_esc);
                 free(val_esc);
                 return NULL;
            }

            total_size += strlen(key_esc) + strlen(val_esc) + 2; // +2 per ':' e '|'

            free(key_esc);
            free(val_esc);
            current_node = current_node->next;
        }
        current_list = current_list->next;
    }

    if (total_size == 0) return strdup("");

    // Alloca il buffer e costruisci la stringa
    char *buffer = malloc(total_size + 1);
    if (!buffer) return NULL;
    buffer[0] = '\0';

    current_list = payload->head;
    while (current_list) {
        strcat(buffer, "[");
        PayloadNode *current_node = current_list->head;

        while (current_node) {
            char *key_esc = escapeString(current_node->key);
            char *val_esc = escapeString(current_node->value);

            if (!key_esc || !val_esc) {
                 free(key_esc);
                 free(val_esc);
                 free(buffer);
                 return NULL;
            }

            strcat(buffer, key_esc);
            strcat(buffer, ":");
            strcat(buffer, val_esc);

            free(key_esc);
            free(val_esc);

            if (current_node->next) {
                strcat(buffer, "|");
            }
            current_node = current_node->next;
        }

        strcat(buffer, "]");
        if (current_list->next) {
            strcat(buffer, ",");
        }

        current_list = current_list->next;
    }

    return buffer;
}


/**
 * Libera la memoria per una lista di PayloadNode.
 */
static void freePayloadNode(PayloadNode *head) {
    PayloadNode *current = head;
    while (current) {
        PayloadNode *next = current->next;
        free(current->key);
        free(current->value);
        free(current);
        current = next;
    }
}

/**
 * Libera la memoria per una lista di PayloadList.
 */
static void freePayloadList(PayloadList *head) {
    PayloadList *current = head;
    while (current) {
        PayloadList *next = current->next;
        freePayloadNode(current->head);
        free(current);
        current = next;
    }
}

/**
 * Libera la memoria per un'intera struttura Payload.
 */
void freePayload(Payload *payload) {
    if (!payload) return;
    freePayloadList(payload->head);
    free(payload);
}


/**
 * Invia un messaggio a un client in modo sicuro, gestendo errori e cleanup automatico.
 * Se l'invio fallisce,  libera le risorse.
 * @return 0 in caso di successo, -1 in caso di errore.
 */
int safeSendMsg(int client_fd, uint16_t msg_type, Payload *payload) {
    char *serialized_payload = serializePayload(payload);
    if (!serialized_payload) {
        freePayload(payload);
        return -1;
    }

    Msg *msg = createMsg(msg_type, strlen(serialized_payload), serialized_payload);
    if (!msg) {
        free(serialized_payload);
        freePayload(payload);
        return -1;
    }
    
    int result = sendMsg(client_fd, msg);

    // Cleanup
    freeMsg(msg);
    freePayload(payload);
    
    return result;
}

/**
 * Riceve un messaggio da un client in modo sicuro, gestendo errori e cleanup automatico.
 * In caso di errore o disconnessione, libera le risorse.
 * @param client_fd File descriptor.
 * @param msg_type_out Puntatore per il tipo di messaggio ricevuto.
 * @param payload_out Puntatore per il Payload deserializzato.
 * @return 0 in caso di successo, -1 in caso di errore o disconnessione.
 */
int safeRecvMsg(int client_fd, uint16_t *msg_type_out, Payload **payload_out) {
    Msg *received_msg = recvMsg(client_fd);
    if (received_msg == NULL) {
        return -1; // Errore o disconnessione
    }

    *msg_type_out = received_msg->header.msgType;
    *payload_out = parsePayload(received_msg->payload);

    if (*payload_out == NULL && received_msg->header.payloadSize > 0) {
        // Errore di parsing
        freeMsg(received_msg);
        return -1;
    }

    freeMsg(received_msg);
    return 0;
}