#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <time.h>

#include "../errlib.h"
#include "../sockwrap.h"

#define MAXLEN 255
#define BACKLOG 15
#define MAXBUFF 1023
#define TIMEOUTSEC 15

#define MSG_OK "+OK"

int sender(int socket, int num_files);

char *prog_name;
/* array of string to memorize the names of the files passed by the command line */
char **file_names;	

int main (int argc, char *argv[]) {

	int socket_fd;
	int i = 0, j = 0, err = 0;
	int number_files, value;
	char *dest_host, *dest_port;
	struct sockaddr_in dest_address;
	struct sockaddr_in *solv_address;
	struct addrinfo *list;

	prog_name = argv[0];

	/* check arguments */
	if (argc < 4){
		err_quit ("How to use: %s <dest_host> <dest_port> <filenames>", prog_name);
	}

	dest_host = argv[1];
	dest_port = argv[2];

	for(i = 3, j = 0; argv[i] != NULL ; i++, j++);
	number_files = j;
	file_names = malloc(number_files * sizeof(char *));
	for(i = 0; i < number_files; i++){
		file_names[i] = malloc(MAXLEN * sizeof(char));
	}
	for(i = 3, j = 0; j < number_files ; i++, j++){
		strcpy(file_names[j], argv[i]);
	}

	Getaddrinfo(dest_host, dest_port, NULL, &list);
	solv_address = (struct sockaddr_in *)list->ai_addr;

	/* create socket */
	socket_fd = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

	/* address to bind */
	memset(&dest_address, 0, sizeof(dest_address));
	dest_address.sin_family = AF_INET;
	dest_address.sin_port = solv_address->sin_port;
	dest_address.sin_addr.s_addr = solv_address->sin_addr.s_addr;

	Connect (socket_fd, (struct sockaddr *)&dest_address, sizeof(dest_address) );

	value = sender(socket_fd, number_files);

	Close (socket_fd);

	return 0;
}

int sender(int socket, int num_files){

	int	i = 0, j = 0, bytes_read = 0;
	char buffer[MAXBUFF];
	char c;
	fd_set fset;
	struct timeval timeout;
	char file_name[MAXLEN+1];
	int n;
	uint32_t file_bytes, timestamp;
	FILE *fp;

	while(i < num_files){
		
		bytes_read = 0;
		/* set timeout */
		FD_ZERO(&fset);
		FD_SET(socket, &fset);
		timeout.tv_sec = TIMEOUTSEC;
		timeout.tv_usec = 0;
		
		sprintf(buffer, "GET %s\r\n", file_names[i]);		
		Write(socket, buffer, strlen(buffer));

		if (select(socket+1, &fset, NULL, NULL, &timeout) > 0) {

			while (bytes_read < MAXBUFF){
				n = Read(socket, &c, sizeof(char));
				if(n == 1){
					buffer[bytes_read] = c;
					bytes_read++;
				}
				else
					break;
				if(c == '\n')
					break;
			}
			if(bytes_read == 0){
				printf("(%s) --- Connection closed by server\n", prog_name);
				return -1;
			}

			buffer[bytes_read]='\0';

			/* remove CR-LF from the buffer */
			while (bytes_read > 0 && (buffer[bytes_read-1]=='\r' || buffer[bytes_read-1]=='\n')) {
				buffer[bytes_read-1]='\0';
				bytes_read--;
			}

			/* check if a +OK is sent by server */
			if (bytes_read == strlen(MSG_OK) && strncmp(buffer,MSG_OK,strlen(MSG_OK)) == 0) {
				strcpy(file_name, file_names[i]);
				n = Read(socket, buffer, 4);
				file_bytes = ntohl((*(uint32_t *)buffer));
				fp = fopen(file_name, "wb");
				if (fp != NULL) {
					for (j = 0; j < file_bytes; j++) {
						Read (socket, &c, sizeof(char));
						fwrite(&c, sizeof(char), 1, fp);
					}
					Read(socket, &timestamp, sizeof(timestamp));
					timestamp = ntohl(timestamp);
					fclose(fp);
					printf("(%s) --- Received file '%s', size: %d bytes, timestamp: %u \n", prog_name, file_name, n, timestamp);
				} 
				else {
					/* cannot create file */
					printf("(%s) --- Error on creating file %s to write\n", prog_name, file_name);
					return -2;
				}
			} 
			else {
				/* received -ERR from server */
				printf("(%s) --- Received -ERR from server\n", prog_name);
				return -3;
			}
		} 
		else {
			/* timeout waiting response */
			printf("(%s) --- Timeout occurred\n", prog_name);
			return -4;
		}
		strcpy(buffer, "\0");
		i++;
	}
	return 0;
}