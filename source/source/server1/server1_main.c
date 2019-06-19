#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <ctype.h>
#include <stdio.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/stat.h>

#include "../sockwrap.h"

#define LISTEN_QUEUE 15
#define MAX_LEN_FILENAME 255
#define DEFAULT_DIM 4096
#define MAX_LINE 1024

#define MACOSX 0 // modifica questo valore a 0 se usi Linux

#if MACOSX
int flags = SO_NOSIGPIPE;
#else
int flags = MSG_NOSIGNAL;
#endif

void reverse (char *s);
void itoa(int n, char *s);
int my_sendn(int sock, uint8_t *ptr, size_t nbytes, int flags);
void sendERR(int sock);

char *prog_name;

int main (int argc, char *argv[])
{
	// 									 	response  dim
	// 											v	   v
	int s, sock, receivingMsgSize, count = 0, i = 0, j = 0, offset, total = 0, sent = 0, n, timeout_sec = 15;
	socklen_t addrSize;
	struct sockaddr_in serverAddr;
	char buffer[MAX_LEN_FILENAME + 6], *ptr, filename[MAX_LEN_FILENAME + 1];
	FILE *fp;
	struct stat statistics;
	uint8_t *response, *start_ptr;
	uint32_t dim = 0, mask, timestamp;
	struct timeval tval;
	fd_set cset;

	// creazione del socket
	if( (s = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ) {
		printf("Error in creating socket.\n");
		return -1;
	}

	// preparazione parametri della bind()
	serverAddr.sin_family = AF_INET;
	serverAddr.sin_port = htons( atoi(argv[1]) );
	serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

	// bind di indirizzo e porta al socket precedentemente creato
	if( bind(s, (const struct sockaddr *) &serverAddr, sizeof(serverAddr)) < 0 ) {
		printf("Error in binding address and port.\n");
		return -2;
	}

	// listen sul socket precedentemente creato con assegnazione della dimensione di una coda di ascolto
	if( listen(s, LISTEN_QUEUE) < 0 ) {
		printf("Error in listen().\n");
		return -3;
	}

	response = (uint8_t *)malloc( DEFAULT_DIM ); // alloco il buffer per la risposta

	while(1) { // in questo ciclo (senza considerare quello ancora piu' interno) si esegue la accept() ovvero si accetta la connessione
			   // da parte di un client
		i = j = total = sent = 0; // inizializzazione delle variabili da usare nella ricezione e nell'invio
		memset(response, 0, DEFAULT_DIM); // azzero il buffer per la risposta
		addrSize = sizeof(serverAddr); // addrSize contiene la dimensione della struct sockaddr_in
		printf("Waiting for a new connection...\n");
		if ( (sock = accept(s, (struct sockaddr *) &serverAddr, &addrSize)) < 0 ) { // eseguo la accept()
			printf("Error in establishing a connection with client.\n");
			continue;
		}
		printf("Connection with client established.\n");
		//printf("HO FATTO LA ACCEPT\n");

		while(1) { // in questo ciclo viene eseguito tutto cio' che serve per ricevere la richiesta del client ed inviare il file richiesto
				   // inizializzazione parametri per fare la select()
			FD_ZERO(&cset);
			tval.tv_sec = timeout_sec;
			tval.tv_usec = 0;
			FD_SET(sock, &cset);

			if( (n = select(FD_SETSIZE, &cset, NULL, NULL, &tval)) == -1 ) { // attendo al massimo 15 secondi che mi arrivi la risposta del server
				printf("Error in select().\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}
			else if(n == 0) { // se non arriva, il server invia la stringa di errore e termina la connessione
				printf("Timeout of %d seconds was reached. Connection with client will be closed.\n", timeout_sec);
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}
			
			receivingMsgSize = Readline(sock, buffer, (size_t) MAX_LEN_FILENAME + 6); //recv(sock, buffer, (size_t) MAX_LEN_FILENAME + 6, 0); // ricezione della richiesta del file
			if( receivingMsgSize < 0 ) {
				printf("Error in receiving data from client.\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}
			if( receivingMsgSize == 0 ) { // non c'e' niente da leggere e il client ha chiuso la connessione: mi metto in attesa di una 
										  // nuova richiesta
				//printf("Client closed connection.\n");
				close(sock);
				break;
			}

			// ricerca della stringa "GET"
			ptr = strstr(buffer, "GET");
			//printf("il comando individuato e': %s e il primo carattere e': 0x%x e il comando e' lungo %d\n", buffer, *buffer, receivingMsgSize);
			if(ptr == NULL) {
				printf("Missing GET command.\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				continue;
			}
			if(ptr != buffer) {
				printf("GET command is not in start position.\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				continue;
			}

			ptr += 3; // ptr dovrebbe puntare al carattere ' '
			if(*ptr != ' ') {
				printf("Missing space character after GET command\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				continue;
			}

			ptr++; // ptr punta al primo carattere del nome del file

			for(; *ptr!='\r' && *(ptr+1)!='\n' && count < MAX_LEN_FILENAME; ptr++, count++); // all'uscita da questo for, ptr punterebbe al
																							 // '\r' che segue il nome del file richiesto
			if(count >= MAX_LEN_FILENAME) { // se ho letto piu' caratteri del dovuto perche' il nome del file non rispetta il massimo numero
											// di caratteri nei sistemi Unix-like
				printf("Unable to find CR-LF in message.\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				continue;
			}

			*(ptr) = '\0'; // metto un '\0' alla fine del nome del file (in modo da terminare una possibile stringa che inizi prima)
			ptr -= count; // ptr punta adesso al primo carattere del nome del file all'interno del buffer di ricezione
			count = 0; // faccio ripartire count da 0 per il prossimo eventuale file richiesto

			if( sscanf(ptr, "%s", filename) != 1 ) { // salvo in filename il nome del file
				printf("Error in retrieving filename.\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}

			// apro il file chiamato filename, se esiste
			if( (fp = fopen(filename, "rb")) == NULL ) {
				printf("Error in opening file %s.\n", filename);
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}

			// recupero le statistiche di quel file (dimensione e data ultima modifica)
			if( stat(filename, &statistics) == -1 ) {
				printf("Error in retrieving information about file %s.\n", filename);
				//perror("");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				fclose(fp);
				break;
			}

			/*
			 *	Da questo momento in poi inizia una serie di test che mirano a capire quando sia il momento migliore per svutoare il buffer
			 *  di invio, ovvero response. In particolare, quando mi rendo conto che, se provassi ad aggiungere altri byte a tale buffer,
			 *  sforerei al di fuori della sua dimensione, invio il buffer e lo svuoto per prepararlo al prossimo rimepimento
			 */

			if( (total + 5*sizeof(char)) >= DEFAULT_DIM ) { // se sforo la dimensione del buffer di invio
				if( my_sendn(sock, response, total, flags) != total ) { // invio il buffer response
					printf("Error in sending response to client.\n");
					sendERR(sock); // mando un messaggio di errore e chiudo la connessione
					break;
				}
				sent += total; // aggiorno il contatore di byte inviati 
				memset(response, 0, DEFAULT_DIM); // azzero il buffer di risposta
				total = 0; // aggiorno il contatore di byte riempiti nel buffer di risposta
			}
			strcpy((char *)response, "+OK\r\n"); // scrivo l'inizio della risposta
			total += 5*sizeof(char); // aggiorno il contatore di byte riempiti nel buffer di risposta
			if(statistics.st_size > 0xFFFFFFFF) { // se la dimensione del file non sta in 32 bit
				printf("Dimension of file %s doesn't fit 32 bit.\n", filename);
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				continue;
			}
			dim = statistics.st_size; // dim contiene la dimensione del file da inviare
			//printf("dim vale: 0x%x che in intero sono %d\n", dim, dim);
			mask = 0xFF000000; // mask e' la maschera che uso per scrivere i byte della dimensione in network byte order (= Big Endian)
			j = 5; // j punta al primo posto libero del buffer di invio (l'if dopo potrebbe cambiarlo nel caso il test sia positivo)
			offset = 24; // offset per lo spostamento del risultato dell'applicazione della maschera alla dimensione dim
			if( (total + sizeof(uint32_t)) >= DEFAULT_DIM ) { // se sforo la dimensione del buffer di invio
				if( my_sendn(sock, response, total , flags) != total - sizeof(uint32_t) ) { // invio il buffer response
					printf("Error in sending response to client.\n");
					sendERR(sock);
					break;
				}
				memset(response, 0, DEFAULT_DIM); // azzero il buffer di risposta
				total = 0; // aggiorno il contatore di byte riempiti nel buffer di risposta
				sent += total; // aggiorno il contatore di byte inviati 
				j = 0; // aggiorno il primo posto libero nel buffer di invio
			}
			// scrivo la dimensione nel buffer di risposta
			for(i = 0; i < 4; i++, mask >>= 8, offset -= 8) {
				response[j++] = ((dim & mask) >> offset);
			}
			total += sizeof(uint32_t); // aggiorno il contatore di byte riempiti nel buffer di risposta	        		   
			if( (total + MAX_LINE) >= DEFAULT_DIM ) { // se sforo la dimensione del buffer di invio
				if( my_sendn(sock, response, total, flags) != total ) { // invio il buffer response
					printf("Error in sending response to client.\n");
					sendERR(sock); // mando un messaggio di errore e chiudo la connessione
					break;
				}
				sent += total; // aggiorno il contatore di byte inviati 
				total = 0; // aggiorno il contatore di byte riempiti nel buffer di risposta
				memset(response, 0, DEFAULT_DIM); // azzero il buffer di risposta
			}
			start_ptr = response; // salvo in start_ptr il valore di response (mi serve per fare una corretta free dopo)
			//printf("response comincia con %c\n", (char)*start_ptr);
			response += total; // response punta al primo byte libero nel buffer di invio
			while( (n = fread(response, sizeof(char), MAX_LINE, fp) ) > 0 ) { // leggo da file MAX_LINE byte salvandoli in response
				total += n; // aggiorno il contatore di byte riempiti nel buffer di risposta
				//printf("total vale %d perche' gli ho appena sommato %d\n", total, n);
				if(n != DEFAULT_DIM && ferror(fp)) { // se ho letto meno di DEFAULT_DIM perche' c'e' stato qualche errore
					printf("Error in reading file %s.\n", filename);
					sendERR(sock); // aggiorno il contatore di byte riempiti nel buffer di risposta
					break;
				}
				if( (total + MAX_LINE) >= DEFAULT_DIM ) { // se sforo la dimensione del buffer di invio
					//printf("Sono entrato con total=%d\n", total);
					response = start_ptr; // recupero l'indirizzo originale di response (mi serve per iniziare dal primo byte del buffer
										  // il nuovo invio)
					if( my_sendn(sock, response, total, flags) != total ) { // invio il buffer response
						printf("Error in sending response to client.\n");
						sendERR(sock); // mando un messaggio di errore e chiudo la connessione
						break;
					}
					sent += total; // aggiorno il contatore di byte inviati 
					total = 0; // aggiorno il contatore di byte riempiti nel buffer di risposta
					memset(response, 0, DEFAULT_DIM); // azzero il buffer di risposta
				}
				else { // altrimenti
					response += n; // mi posiziono per salvare la lettura nell'indirizzo n byte piu' avanti rispetto a dove ho appena
								   // iniziato a scrivere
					//printf("durante il ciclo response comincia con %c\n", (char)*response);
				}
			}
			response = start_ptr; // recupero l'indirizzo originale di response (mi serve per iniziare dal primo byte del buffer
								  // il nuovo invio)
			//printf("response comincia con %c\n", (char)*response);
			//printf("Ho finito di mandare il il file %s\n", filename);
			fclose(fp); // chiudo il file precedentemente aperto

			if(statistics.st_ctime > 0xFFFFFFFF) { // se la dimensione del tempo di ultima modifica non sta in 32 bit
				printf("Timestamp of last change on file %s doesn't fit 32 bit.\n", filename);
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}
			timestamp = statistics.st_ctime; // timestamp contiene il tempo di ultima modifica
			//printf("timestamp vale: 0x%x\n", timestamp);
			mask = 0xFF000000; // mask e' la maschera che uso per scrivere i byte del tempo di ultima modifica in network byte order (= Big Endian)
			offset = 24; // offset per lo spostamento del risultato dell'applicazione della maschera al tempo di ultima modifica timestamp
			if( (total + sizeof(uint32_t)) >= DEFAULT_DIM ) { // se sforo la dimensione del buffer di invio
				if( my_sendn(sock, response, total, flags) != total - sizeof(uint32_t) ) { // invio il buffer response
					printf("Error in sending response to client.\n");
					sendERR(sock); // mando un messaggio di errore e chiudo la connessione
					break;
				}
				memset(response, 0, DEFAULT_DIM); // azzero il buffer di risposta
				sent += total - sizeof(uint32_t); // aggiorno il contatore di byte inviati 
				total = 0; // aggiorno il contatore di byte riempiti nel buffer di risposta
			}
			// scrivo il tempo di ultima modifica nel buffer di risposta
			for(i = 0, j = 0; i < 4; i++, j++, mask >>= 8, offset -= 8) {
				response[j+total] = ((timestamp & mask) >> offset);
			}
			total += sizeof(uint32_t); // aggiorno il contatore di byte riempiti nel buffer di risposta
			//printf("Qui ci arrivo con j=%d\n", j);
			//printf("sto inviando %d byte\n", total);
			if( my_sendn(sock, response, total, flags) != total ) { // invio il buffer response
				printf("Error in sending response to client.\n");
				sendERR(sock); // mando un messaggio di errore e chiudo la connessione
				break;
			}
			memset(response, 0, DEFAULT_DIM); // azzero il buffer di risposta
			sent += total; // aggiorno il contatore di byte inviati 
			total = 0; // aggiorno il contatore di byte riempiti nel buffer di risposta
			
			//response_dim = sent;
			//printf("response_dim vale: %d\n", response_dim);
		}
	}
	free(response);

	return 0;
}

void reverse (char *s)
{
	int c, i, j;

  	for (i=0, j=strlen(s)-1; i<j; i++, j--)  {
    	c = s[i];
    	s[i] = s[j];
    	s[j] = c;
  	}
}

void itoa(int n, char *s)
{
	int i, sign;

	if ((sign = n)<0)
    	n = -n;
  	
  	i = 0;
  	
  	do {
    	s[i++] = n%10 + '0';
  	} 
  	while ((n /= 10) > 0);
  	
  	if (sign<0)
    	s[i++] = '-';
  	
  	s[i] = '\0';
  	
  	reverse(s);
}

int my_sendn(int sock, uint8_t *ptr, size_t nbytes, int flags) {
	size_t nleft;
	ssize_t nwritten;
    
    for (nleft=nbytes; nleft > 0; ) {
    	nwritten = send(sock, ptr, nleft, flags);
		if (nwritten <=0)
		 	return (nwritten);
        else {
            nleft -= nwritten;
            ptr += nwritten;
		} 
	}

    return (nbytes - nleft);
}

void sendERR(int sock) {
	char response[6] = "-ERR\r\n";

	if( send(sock, response, 6, flags) != 6 ) {
		printf("Error in sending error message response.\n");
		return;
	}
	close(sock);

	return;
}
