/*
 
 module: sockwrap.c
 
 purpose: library of wrapper and utility socket functions
          wrapper functions include error management
 
 reference: Stevens, Unix network programming (3ed)
 
 */


#include <stdlib.h> // getenv()
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

//#include "errlib.h"
#include "sockwrap.h"

extern char *prog_name;


#ifndef MAXLINE
#define MAXLINE 1024
#endif

/* read a whole buffer, for performance, and then return one char at a time */
static ssize_t my_read (int fd, char *ptr)
{
	static int read_cnt = 0;
	static char *read_ptr;
	static char read_buf[MAXLINE];

	if (read_cnt <= 0)
	{
again:
		if ( (read_cnt = read(fd, read_buf, sizeof(read_buf))) < 0)
		{
			if (INTERRUPTED_BY_SIGNAL)
				goto again;
			return -1;
		}
		else
			if (read_cnt == 0)
				return 0;
		read_ptr = read_buf;
	}
	read_cnt--;
	*ptr = *read_ptr++;
	return 1;
}

/* NB: Use my_read (buffered recv from stream socket) to get data. Subsequent readn() calls will not behave as expected */
ssize_t readline (int fd, void *vptr, size_t maxlen)
{
	int n, rc;
	char c, *ptr;

	ptr = vptr;
	for (n=1; n<maxlen; n++)
	{
		if ( (rc = my_read(fd,&c)) == 1)
		{
			*ptr++ = c;
			if (c == '\n')
				break;	/* newline is stored, like fgets() */
		}
		else if (rc == 0)
		{
			if (n == 1)
				return 0; /* EOF, no data read */
			else
				break; /* EOF, some data was read */
		}
		else
			return -1; /* error, errno set by read() */
	}
	*ptr = 0; /* null terminate like fgets() */
	return n;
}


ssize_t Readline (int fd, void *ptr, size_t maxlen)
{
	ssize_t n;

	if ( (n = readline(fd, ptr, maxlen)) < 0)
		printf("Error\n");
	return n;
}

