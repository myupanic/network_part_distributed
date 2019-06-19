/*
 * TEMPLATE 
 */
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <endian.h>
#include "../sockwrap.h"

#define MAX_TIMEOUT_S 15
#define BUFLEN 1024 /* BUFFER LENGTH: 1 KB */
#define MAXCOMMANDNAME 270


const char errorString[] = "-ERR\r\n";
const char okString[] = "+OK\r\n";
const int okLength = sizeof(okString);

// Program name
char *prog_name;

// Network
char *sPortNumber;
char *stringIPaddress;
uint16_t tport_n;	/* server port number (net/host ord) */
int s; //socket number
struct sockaddr_in	saddr;		/* server address structure */
struct in_addr	sIPaddr; 	/* server IP addr. structure */



void init();
void readFile(char **fileList, int index);
void requestFile(char **fileList, int index);
void setSocketTimeout();

int main (int argc, char *argv[])
{


    prog_name = argv[0];
	if(argc < 4)
	{
		printf("Usage: %s <destination_address> <port> <file_1> <file_2> ...\n", prog_name);
		exit(-1);
	}
	if (sscanf(argv[1], "%" SCNu16, &tport_n)!=1)
	{
		printf("Invalid port number\n");
		exit(-1);
	}
	stringIPaddress = argv[1];
	sPortNumber = argv[2];
	init();
	for(int i = 3; i < argc; ++i)
	{
		setSocketTimeout();
		requestFile(argv, i);
		readFile(argv, i);
	}

	Close(s);
	return 0;
}


void init()
{
	Inet_aton(stringIPaddress, &sIPaddr);
	printf("Creating socket\n");
	s = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	tport_n = htons(atoi(sPortNumber));

	bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port   = tport_n;
    saddr.sin_addr   = sIPaddr;

	showAddr("Connecting to target address:\n", &saddr);
    Connect(s, (struct sockaddr *) &saddr, sizeof(saddr));
    printf("Initialization done.\n");

}

void readFile(char** fileList, int index)
{
	uint32_t fileSize = 0;
	char rbuf[BUFLEN];	/* reception buffer */
	size_t nReadChars = 0;
	size_t nWrChars = 0;
	uint32_t timeStamp;
	char isFirstStart = 1;
	int fd;

	// First reading
	nReadChars = read(s, rbuf, okLength-1);
	if(!nReadChars)
	{
		printf("Server could not send %s\n", fileList[index]);
		return;
	}
	if(!strncmp(rbuf, errorString, okLength))
	{
		printf("Error in the file request.\n");
		return;
	}
	Read(s, &fileSize, sizeof(fileSize));
	fileSize = be32toh(fileSize);
	if(fileSize == 0)
	{
		printf("Connection timed out\n");
		return;
	}
	fd = open(fileList[index], O_CREAT | O_WRONLY | O_TRUNC, S_IRWXU);
	if(fd < 0 )
	{
		printf("Error in file creation\n");
		Close(s);
		exit(-1);
	}

	printf("Receiving data\n");
	nWrChars = 0;
// READING FILE
	while(fileSize > nWrChars)
	{
		if(fileSize - nWrChars < BUFLEN)
		{
			nReadChars = Read(s, rbuf, fileSize - nWrChars);
		}
		else
		{
			nReadChars = Read(s, rbuf, BUFLEN);
		}
		
		nWrChars += write(fd, rbuf, nReadChars);
	}

	Read(s, &timeStamp, sizeof(timeStamp));
	timeStamp = be32toh(timeStamp);
	printf("END OF TRANSFER\n\tFile name: %s\n\tFile size: %u\n\tLast modification: %u\n",
			fileList[index], fileSize, timeStamp);
	fflush(stdout);
	Close(fd);
}

void requestFile(char **fileList, int index)
{
	char buf[BUFLEN];	/* transmission buffer */
	int writeChars;
	sprintf(buf, "GET %s\r\n", fileList[index]);
	printf("\nAsking for file %s...\n", fileList[index]);
	writeChars = write(s, buf, strlen(buf));
	if(writeChars != strlen(buf))
	{
		printf("Sending error!\n");
		Close(s);
		exit(-1);
	}
	printf("Request sent.\n");
}

void setSocketTimeout()
{
	//Set socket timeout
	struct timeval timeout;
	timeout.tv_sec = MAX_TIMEOUT_S;
	timeout.tv_usec = 0;
	printf("Setting socket timeout to 15 secs.\n");
	Setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	Setsockopt(s, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

}