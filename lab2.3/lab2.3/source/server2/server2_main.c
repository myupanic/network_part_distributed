/*
 * TEMPLATE 
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <endian.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include "../sockwrap.h"

#define MAX_TIMEOUT_S 15
#define BUFLEN	1024 /* BUFFER LENGTH */
#define MAXCOMMANDNAME 270

char errorString[] = "-ERR\r\n";
char okString[] = "+OK\r\n";
int okStringLength = sizeof(okString);

// Program name
char *prog_name;
char *sPortNumber;
uint16_t lport_n, lport_h;	/* server port number (net/host ord) */
int s, conn_reqest_socket; // socket number
int nSockets; // keep track of the number of sockets allocated
int backlog = 2; // backlog number
socklen_t 	addrlen;
struct sockaddr_in saddr, caddr;		/* server and client addresses */

void init();
void* serve(int socket);
uint32_t getFileSize(int fd);
uint32_t getFileLastModification(int fd);
int getFileRequest(int socket, char* result);
void sendError(int socket, char* serverLog);
void sendFile(int socket, char* fileRequested);
void setSocketTimeout(int socket, int sec);

int main (int argc, char *argv[])
{
    prog_name = argv[0];
	if(argc < 2)
	{
		printf("Usage: %s <port>\n", prog_name);
		exit(-1);
	}
	if (sscanf(argv[1], "%" SCNu16, &lport_h)!=1)
	{
		printf("Invalid port number\n");
		exit(-1);
	}
	sPortNumber = argv[1];
	nSockets = 1;
	init(s);
	Listen(s, backlog);
	conn_reqest_socket = s;
	while(1)
	{

		s = Accept(conn_reqest_socket, (struct sockaddr*)&caddr, &addrlen);
		showAddr("Accepted connection from", &caddr);
		printf("new socket: %u\n",s);
		if(!fork())
		{
			serve(s);
			exit(0);
		}
	}
	return 0;
}


void init()
{
	printf("Creating listening socket\n");
	s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	lport_n = htons(atoi(sPortNumber));

	bzero(&saddr, sizeof(saddr));
    saddr.sin_family 		= AF_INET;
    saddr.sin_port   		= lport_n;
    saddr.sin_addr.s_addr	= INADDR_ANY;

	showAddr("Binding server to address", &saddr);
    Bind(s, (struct sockaddr *) &saddr, sizeof(saddr));
    printf("Binding done.\n");

}

// Service function for each socket
void* serve(int socket)
{
	char fileRequested[MAXCOMMANDNAME];
	int result = 1;
	setSocketTimeout(socket, MAX_TIMEOUT_S);
	do
	{
		fileRequested[0] = '\0';
		result = getFileRequest(socket, fileRequested);
		if(result > 0)
		{
			printf("Requested file: %s\n", fileRequested);
			sendFile(socket, fileRequested);
		}
		else if(result == -1) break;


	} while(fileRequested[0] != '\0');

	printf("Finished serving socket %u: closing connection.\n", socket);
	Close(socket);
}

uint32_t getFileSize(int fd)
{
	uint32_t correctSize; //the size in the correct format
	struct stat fileStat;
	fstat(fd, &fileStat);
	correctSize = htobe32(fileStat.st_size);
	return correctSize;
}

uint32_t getFileLastModification(int fd)
{
	uint32_t correctTime; //the size in the correct format
	struct stat fileStat;
	fstat(fd, &fileStat);
	correctTime = htobe32(fileStat.st_mtime);
	return correctTime;
}

int getFileRequest(int socket, char* result)
{
	char* end;
	int charsRead = readline(socket, result, MAXCOMMANDNAME);
	if(charsRead == 0 || result[0] == '\0')
	{
		return -1;
	}
	if(strncmp("GET ", result, 4))
	{
		sendError(socket, "Expected GET request");
		return 0;
	}
	if(charsRead == MAXCOMMANDNAME)
	{
		sendError(socket, "File name too long!");
		return 0;
	}
	end = strstr(result, "\r\n");
	if(!end)
	{
		sendError(socket, "Malformed request!");
		return 0;
	}
	*end = '\0';
	for(int i = 0; result[i+4] != *end; ++i)
	{
		result[i] = result[i+4];
		result[i+1] = '\0';
	}
	return 1;
}

void sendError(int socket, char* serverLog)
{
	printf("%s\n", serverLog);
	Write(socket, errorString, strlen(errorString));
}

void sendFile(int socket, char* fileRequested)
{
	char outBuffer[BUFLEN] = {'\0'};
	int fd;
	int readChars = 0, sentChars = 0;
	uint32_t size, timeStamp;
	fd = open(fileRequested, O_RDONLY);
	if(fd < 0)
	{
		sendError(socket, "Unable to reach the file.\n");
		return;
	}
	size = getFileSize(fd);
	timeStamp = getFileLastModification(fd);
	Write(socket, okString, okStringLength - 1);
	Write(socket, &size, sizeof(size));
	size = be32toh(size);
	// File sending
	while(sentChars < size)
	{
		readChars = Read(fd, outBuffer, BUFLEN);
		sentChars += writen(socket, outBuffer, readChars);
	}
	Write(socket, &timeStamp, sizeof(timeStamp));
	close(fd);
}

void setSocketTimeout(int socket, int sec)
{
	//Set socket timeout
	struct timeval timeout;
	timeout.tv_sec = sec;
	timeout.tv_usec = 0;
	printf("Setting socket timeout to 15 secs.\n");
	Setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	Setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

}
