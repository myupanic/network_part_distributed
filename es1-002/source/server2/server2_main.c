#include <stdlib.h>
#include <ctype.h>
#include <string.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>

#include "../errlib.h"
#include "../sockwrap.h"

#define MSG_ERR "-ERR\r\n"
#define MSG_OK  "+OK\r\n"
#define MSG_GET "GET"

#define MAXLEN 255
#define BACKLOG 15
#define MAXBUFF 1023
#define TIMEOUTSEC 15

char *prog_name;


int receiver(int connection_fd);
void sig_handler(int sign);


int main (int argc, char *argv[]) {

	int listen_fd, connection_fd, value;
	int port;
	struct sockaddr_in server_address, client_address;
	socklen_t c_address_len = sizeof(client_address);
	pid_t childpid;

	prog_name = argv[0];

	if (argc != 2)
		err_quit ("How to use: %s <port>\n", prog_name);
	port = atoi(argv[1]);

	/* create socket */
	listen_fd = Socket(AF_INET, SOCK_STREAM, 0);

	/* address to bind */
	memset(&server_address, 0, sizeof(server_address));
	server_address.sin_family = AF_INET;
	server_address.sin_port = htons(port);
	server_address.sin_addr.s_addr = htonl (INADDR_ANY);

	Bind(listen_fd, (struct sockaddr *) &server_address, sizeof(server_address));

	Listen(listen_fd, BACKLOG);
	
	signal(SIGCHLD, sig_handler);

	while (1) {

		connection_fd = Accept (listen_fd, (struct sockaddr *) &client_address, &c_address_len);
		childpid = fork();
		if(childpid < 0){
			Close(connection_fd);
			err_quit("fork() failed");
		}
		else if(childpid == 0){
			/* child process */
			Close(listen_fd);
			value = receiver(connection_fd);
			if(value == 0)
				printf("(%s) --- Connection closed by client\n", prog_name);
			exit(value);
		}
		else{
			/* parent process */
			Close(connection_fd);
		}
	}

	return 0;
}

int receiver(int connection_fd){

	char buffer[MAXBUFF+1], file_name[MAXLEN+1];
	char c;
	fd_set fset;
	struct timeval timeout;
	int bytes_read;
	int i = 0;
	struct stat info;
	int ret_stat, n, size;
	uint32_t val, timestamp;
	FILE *fp;

	while (1) {

		bytes_read = 0; 

		/* set timeout */
		FD_ZERO(&fset);
		FD_SET(connection_fd, &fset);
		timeout.tv_sec = TIMEOUTSEC;
		timeout.tv_usec = 0;

		if (select(connection_fd + 1, &fset, NULL, NULL, &timeout) > 0) {
			while (bytes_read < MAXBUFF){
				n = Read(connection_fd, &c, sizeof(char));
				if (n == 1){
					buffer[bytes_read] = c;
					bytes_read++;
				}
				else
					break;
				if(c == '\n')
					break;
			}
			if (bytes_read == 0)
				return 0;
			buffer[bytes_read]='\0';

			/* remove CR-LF from the buffer */
			while (bytes_read > 0 && (buffer[bytes_read-1]=='\r' || buffer[bytes_read-1]=='\n')) {
				buffer[bytes_read-1]='\0';
				bytes_read--;
			}

			/* manage the command received */
			if (bytes_read > strlen(MSG_GET) && strncmp(buffer,MSG_GET,strlen(MSG_GET)) == 0) {
				strcpy(file_name, buffer + 4);
				ret_stat = stat(file_name, &info);
				if (ret_stat == 0) {
					fp = fopen(file_name, "rb");
					if (fp != NULL) {
						Write (connection_fd, MSG_OK, strlen(MSG_OK));
						size = info.st_size;
						val = htonl(size);
						Write (connection_fd, &val, sizeof(size));
						for (i = 0; i < size; i++) {
							fread(&c, sizeof(char), 1, fp);
							Write (connection_fd, &c, sizeof(char));
						}
						timestamp = htonl(info.st_mtim.tv_sec);
						Write(connection_fd, &timestamp, sizeof(timestamp));
						fclose(fp);
					}
					else {
						printf("(%s) --- Error opening file()\n", prog_name);
						Write (connection_fd, MSG_ERR, strlen(MSG_ERR) );
						Close(connection_fd);
						return -1;
					}
				}
				else {	
					printf("(%s) --- Error on function stat()\n", prog_name);
					Write (connection_fd, MSG_ERR, strlen(MSG_ERR));
					Close(connection_fd);
					return -2;
				}
			}
			else {
				printf("(%s) --- Unknown message from client\n", prog_name);
				Write (connection_fd, MSG_ERR, strlen(MSG_ERR));
				Close(connection_fd);
				return -3;
			}
		}
		else{
			printf("(%s) --- Timeout waiting command from client\n", prog_name);
			Write (connection_fd, MSG_ERR, strlen(MSG_ERR));
			Close(connection_fd);
			return -3;
		}
		strcpy(buffer, "\0");
	}

	Close(connection_fd);

	return 0;
}

void sig_handler(int sign) {
	int status;
	while ((waitpid(-1, &status, WNOHANG)) > 0);
	return;
}