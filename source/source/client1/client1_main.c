#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h> // timeval
#include <sys/select.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <arpa/inet.h> // inet_aton()
#include <sys/un.h> // unix sockets
#include <netdb.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <inttypes.h> // SCNu16
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define DEFAULT_DIM 4096

char *prog_name;

int main (int argc, char *argv[])
{
	struct sockaddr_in saddr;
	int s;
	int result;
	char *buf;
	uint8_t received[DEFAULT_DIM]; // 4 KB buffer to receive data
	int received_len = 0;
	int timeout_sec = 15, n;
	struct timeval tval;
	fd_set cset;
	FILE *fp;
	int i, j, k;
	uint32_t size = 0, timestamp = 0;
	int val, total_file = 0, to_be_read, file_finished = 0;

	// creazione del socket
	s = socket(PF_INET,SOCK_STREAM,IPPROTO_TCP);
	if( s < 0 )
	{
		printf("Error in creating socket.\n");
		return -2;
	}
	// preparazione parametri della connect()
	saddr.sin_family = AF_INET;
	if( inet_aton(argv[1],&(saddr.sin_addr)) == 0 ) {
		printf("Error in inet_aton() for address %s.\n", argv[1]);
	}
	saddr.sin_port = htons( atoi(argv[2]) );

	// creazione della connessione tra client e socket
	result = connect( s, (struct sockaddr *) &saddr, sizeof(saddr) );	
	if( result == -1 )
	{
		printf("Error in establishing a connection to %s on port %s.\n", argv[1], argv[2]);
		if( close(s) != 0 )
		{
			printf("Error in closing socket.\n");
			return -3;
		}
		return -4;
	}

	for(k=0; k < argc - 3; k++) { // k cicla sui nomi dei file da scaricare
		buf = (char *)malloc( strlen(argv[k+3])*sizeof(char) + 1 + 5 ); // alloco il buffer dimensionandolo sul nome del k-esimo file da scaricare
		strcpy(buf, "GET "); // il comando da inviare deve iniziare con "GET "
		strcat(buf, argv[k+3]); // poi si deve specificare il nome del file k-esimo da scaricare
		buf += strlen(argv[k+3]) + 4; // buf adesso punta al carattere successivo a quello del nome del file k-esimo da scaricare
		*buf = '\r'; // il comando termina con "\r\n" (CR-LF)
		*(buf+1) = '\n';
		buf -= ( strlen(argv[k+3]) + 4 ); // buf punta nuovamente all'indirizzo originario (serve per fare la free dopo)

		if( send( s, buf, (size_t) strlen(argv[k+3]) + 6, 0 ) != strlen(argv[k+3]) + 6 ) { // invio il comando al server
			printf("Error in sending parameters to server.\n");
			return -1;
		}

		// inizializzazione parametri per fare la select()
		FD_ZERO(&cset);
		tval.tv_sec = timeout_sec; // attendo al massimo 15 secondi
		tval.tv_usec = 0;
		FD_SET(s, &cset);

		if( (n = select(FD_SETSIZE, &cset, NULL, NULL, &tval)) == -1 ) { // attendo al massimo 15 secondi che mi arrivi la risposta del server
			printf("Error in select().\n");
			return -2;
		}
		else if(n == 0) { // se non arriva, il client termina segnalando un errore
			printf("Timeout of %d seconds reached. The connection has been closed.\n", timeout_sec);
			return -3;
		}
		else {
			received_len = size = timestamp = total_file = to_be_read = file_finished = 0; // inizializzazione delle variabili da usare nella ricezione
			val = recv(s, received, 5*sizeof(char) + sizeof(uint32_t), 0); // ricezione dei primi byte della risposta ("+OK\r\nB1B2B3B4")
			if(val == 0) { // il server ha chiuso la connessione e non e' rimasto nulla da leggere
				printf("The connection has been closed. Nothing to be read.\n");
				return -4;
			}
			if(val < 0) {
				printf("Error in receiving response from server %s.\n", argv[1]);
				return -5;
			}
			received_len += val; // aggiorno la lunghezza della risposta
			//printf("finora ho ricevuto %d byte\n", received_len);
			i = 5; // i verra' usato dopo per puntare al primo byte della dimensione nella risposta
			for(j = 0; j < 4; j++) {
				size += received[i++]; // aggiorno il byte piu' significativo di size
				if(j != 3) { // se non e' l'ultimo byte dei 4
					size <<= 8; // traslo a sinistra di 8 bit il valore di size
				}
			}
			//printf("size vale: 0x%x che in intero sono %d\n", size, size);

			memset(received, 0, DEFAULT_DIM); // received viene azzerato per la prossima ricezione

			if( (fp = fopen(argv[k+3], "wb")) == NULL ) { // creo (o apro se gia' c'era) il file da scaricare dal server
				printf("Error in opening file %s.\n", argv[k+3]);
				free(buf);
				return -6;
			}

			while( file_finished == 0 ) { // finche' il file da scaricare non e' stato scaricato del tutto
				//printf("ricevuti %d byte di %d\n", total_file, size);
				if( total_file + DEFAULT_DIM >= size ) { // se la dimensione finora scaricata del file + la dimensione che mi appresto a scaricare di
														 // esso supera la dimensione vera del file
					to_be_read = size - total_file; // i byte che devo ancora ricevere sono size - dimensione del file finora scaricata
					file_finished = 1; // segnalo che ho finito di ricevere il file
					//printf("ho finito di ricevere il file %s: mancano giusto gli ultimi %d byte\n", argv[k+3], to_be_read);
				}
				else { // altrimenti
					to_be_read = DEFAULT_DIM; // devo leggere un intero buffer di ricezione
				}
				FD_ZERO(&cset);
				tval.tv_sec = timeout_sec; // attendo al massimo 15 secondi
				tval.tv_usec = 0;
				FD_SET(s, &cset);

				if( (n = select(FD_SETSIZE, &cset, NULL, NULL, &tval)) == -1 ) { // attendo al massimo 15 secondi che mi arrivi la risposta del server
					printf("Error in select().\n");
					return -2;
				}
				else if(n == 0) { // se non arriva, il client termina segnalando un errore
					printf("Timeout of %d seconds reached. The connection has been closed.\n", timeout_sec);
					return -3;
				}
				val = recv(s, received, to_be_read, 0); // leggo l'opportuno (appena calcolato) numero di byte
				//printf("ho ricevuto %d byte del file %s\n", val, argv[k+3]);
				if( val == 0 ) { // il server ha chiuso la connessione e non e' rimasto nulla da leggere
					printf("Connection has been closed: nothing to be read.\n");
					return -8;
				}
				if( val < 0 ) {
					printf("Error in receiving response from server %s.\n", argv[1]);
					return -9;
				}
				if( fwrite(received, sizeof(char), val, fp) != val) { // scrivo i byte appena ricevuti sul file creato in precedenza
					printf("Error in writing on file %s.\n", argv[k+3]);
					return -7;
				} 
				total_file += val; // aggiorno il numero di byte scaricati correttamente
				received_len += val; // aggiorno la lunghezza dell'intera risposta
				memset(received, 0, DEFAULT_DIM); // received viene azzerato per la prossima ricezione
			}
			//printf("DORMO 8 SECONDI\n");
			//sleep(8);
			//printf("Mi sono svegliato e mi manca poco per dichiarare scaricato il file %s\n", argv[k+3]);
			fclose(fp); // chiudo il file

			//printf("DORMO 10 SECONDI\n");
			//sleep(10);

			FD_ZERO(&cset);
			tval.tv_sec = timeout_sec; // attendo al massimo 15 secondi
			tval.tv_usec = 0;
			FD_SET(s, &cset);

			if( (n = select(FD_SETSIZE, &cset, NULL, NULL, &tval)) == -1 ) { // attendo al massimo 15 secondi che mi arrivi la risposta del server
				printf("Error in select().\n");
				return -2;
			}
			else if(n == 0) { // se non arriva, il client termina segnalando un errore
				printf("Timeout of %d seconds reached. The connection has been closed.\n", timeout_sec);
				return -3;
			}
			val = recv(s, received, sizeof(uint32_t), 0); // leggo il timestamp
			if(val == 0) { // il server ha chiuso la connessione e non e' rimasto nulla da leggere
				printf("The connection has been closed. Nothing to be read.\n");
				return -10;
			}
			if(val < 0) {
				printf("Error in receiving response from server %s.\n", argv[1]);
				return -11;
			}
			received_len += val; // aggiorno la lunghezza dell'intera risposta
			//printf("fin qui ho ricevuto %d byte\n", received_len);
			for(i = 0, j = 0; j < 4; j++) {
				timestamp += received[i++]; // aggiorno il byte piu' significativo di timestamp
				if(j != 3) { // se non e' l'ultimo byte dei 4
					timestamp <<= 8; // traslo a sinistra di 8 bit il valore di size
				}
			}

			free(buf); // libero il buffer

			printf("File %s correctly downloaded.\nSize: %d bytes\nSeconds passed from last change (starting from epoch): %d\n", argv[k+3], size, timestamp);
		}
	}

	// chiudo il socket
	close(s);

	return 0;
}
