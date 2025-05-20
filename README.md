# Battaglia navale multiutente
## Specifiche del progetto
Realizzazione di una versione elettronica del famoso gioco "battaglia navale" con un numero di giocatori arbitrario.
In questa versione più processi client (residenti in generale su macchine diverse) sono l'interfaccia tra i giocatori e il server (residente in generale su una macchina separata dai client). Un client, una volta abilitato dal server, accetta come input una mossa, la trasmette al server, e riceve la risposta dal server. In questa versione della battaglia navale una mossa consiste oltre alle due coordinate anche nell'identificativo del giocatore contro cui si vuole far fuoco.
Il server a sua volta quando riceve una mossa, comunica ai client se qualcuno è stato colpito, se uno dei giocatori è il vincitore (o se è stato eliminato), altrimenti abilita il prossimo client a spedire una mossa.
La generazione della posizione delle navi per ogni client è lasciata alla discrezione dello studente.
