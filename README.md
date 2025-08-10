
# ⚓️ Battaglia Navale Multiutente

## Introduzione e Specifiche del Progetto

Questo progetto realizza una versione elettronica e multi-giocatore del classico gioco "Battaglia Navale". L'architettura si basa su un modello client-server, dove più client, potenzialmente su macchine diverse, si connettono a un server centrale per partecipare alla stessa partita.

**Le specifiche principali del gioco:**

- I giocatori, tramite l'interfaccia client, possono creare nuove partite o unirsi a partite esistenti.
- Una mossa consiste nello specificare le coordinate del colpo e il giocatore avversario da attaccare.
- Il server riceve le mosse, ne valida la legittimità (es. turno corretto), e notifica a tutti i client l'esito del colpo (mancato, colpito, affondato).
- Il server gestisce la progressione dei turni, decretando vittoria, eliminazione dei giocatori e la conclusione della partita.


## Architettura e Scelte di Progetto

Il sistema è progettato per essere robusto, scalabile e reattivo, utilizzando tecniche di programmazione concorrente e di rete.

### Architettura del Server

Il server è il cuore del sistema e gestisce tutta la logica di gioco. È implementato con un'architettura multi-threaded per gestire simultaneamente più connessioni e partite.

- **Thread Principale:** inizializza il server, ascolta sulla porta specificata e accetta nuove connessioni TCP. Ogni nuova connessione viene passata al thread della lobby tramite una pipe.
- **Thread della Lobby** (`lobbyManager.c`): gestisce i client non ancora in partita. Usa epoll per multiplexare efficientemente l'I/O di tutti i client connessi. Si occupa di:
	- Gestire il login degli utenti
	- Permettere la creazione di nuove partite (`MSG_CREATE_GAME`)
	- Permettere ai giocatori di unirsi a partite esistenti (`MSG_JOIN_GAME`)
- **Thread di Gioco** (`gameManager.c`): per ogni partita viene creato un thread dedicato che gestisce:
	- La fase di preparazione (posizionamento flotte)
	- L'avvio della partita e la generazione casuale dell'ordine dei turni
	- La logica di attacco, validazione delle mosse e aggiornamento dello stato di gioco per tutti i partecipanti

### Architettura del Client

Il client fornisce l'interfaccia utente per il gioco. Anche il client è multi-threaded per separare la gestione dell'I/O di rete dalla logica dell'interfaccia utente.

- **Thread Principale** (`client.c`, `clientGameManager.c`): gestisce la connessione al server, il menu iniziale (creazione/unione partita) e la comunicazione di rete durante il gioco. Riceve i messaggi dal server tramite epoll e aggiorna lo stato del gioco locale (`GameState`).
- **Thread UI** (`gameUI.c`): responsabile del rendering dell'interfaccia di gioco nel terminale, inclusa la griglia, i log degli eventi e la gestione dell'input utente (movimento cursore, posizionamento navi, attacco). Comunica le azioni dell'utente al thread principale tramite una pipe. L'uso di un thread separato garantisce reattività anche con latenza di rete.

### Protocollo di Comunicazione (`protocol.c`)

La comunicazione tra client e server si basa su un protocollo custom TCP. I messaggi sono strutturati con:

- **Header:** contiene il tipo di messaggio (`msgType`) e la dimensione del payload (`payloadSize`).
- **Payload:** stringa formattata con coppie chiave-valore (es. `[key1:value1|key2:value2],[key3:value3]`), serializzata prima dell'invio e deserializzata alla ricezione. Questa struttura permette di inviare dati complessi in modo strutturato.
- Le funzioni `safeSendMsg` e `safeRecvMsg` garantiscono l'invio/ricezione completa dei messaggi, gestendo la natura a flusso di TCP.

### Gestione Dati e Concorrenza

- `users.c` e `list.c`: sul server, le informazioni su utenti e partite sono memorizzate in liste concorrenti custom. La struttura dati `ListManager` è thread-safe e usa un array di pagine per evitare riallocazioni costose e mutex a livello di nodo/lista per accesso concorrente efficiente.
- **Mutex:** uso estensivo di `pthread_mutex_t` su client e server per accesso sicuro alle strutture dati condivise tra thread, prevenendo race condition.


## Manuale d'Uso

### Compilazione

Il progetto utilizza un Makefile. È sufficiente avere `gcc` e `make` installati.

Aprire un terminale nella directory radice del progetto ed eseguire uno dei seguenti comandi:

- **Per compilare sia client che server:**
	```bash
	make all
	```
- **Solo server:**
	```bash
	make server
	```
- **Solo client:**
	```bash
	make client
	```
- **Pulizia (rimuove eseguibili e oggetti):**
	```bash
	make clean
	```

I file eseguibili verranno creati nella directory `bin/`.

### Esecuzione

**Server**

Per avviare il server, specificare la porta su cui ascoltare:

```bash
./bin/server -port <numero_porta>
```
Esempio:
```bash
./bin/server -port 8888
```

**Client**

Per avviare il client, specificare l'indirizzo IP (o hostname) del server e la porta:

```bash
./bin/client -address <indirizzo_server> -port <numero_porta>
```
Esempio:
```bash
./bin/client -address localhost -port 8888
```

Una volta connesso, il client richiederà di inserire un nome utente e presenterà un menu per creare una nuova partita o unirsi a una esistente.
