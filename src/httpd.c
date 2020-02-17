#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include "httpd.h"

#define PORT "8000"
#define MAX_CONNECTIONS 1000
#define NOCLIENT -1
static int listenfd;
static int clientfd;
static int *clients;
static char *buf;

/*
 * Respond to a client connection.
 */
static void
respond(const size_t i)
{
	int rcvd;

	buf  = malloc(65535);
	rcvd = recv(clients[i], buf, 65535, 0);

	if (rcvd < 0) // receive error
	{
		fprintf(stderr, "recv() error\n");
	}
	else if (rcvd == 0) // receive socket closed
	{
		fprintf(stderr, "Client disconnected upexpectedly.\n");
	}
	else
	{
		buf[rcvd] = '\0';
		method    = strtok(buf,  " \t\r\n");
		uri       = strtok(NULL, " \t");
		prot      = strtok(NULL, " \t\r\n");

		T_INFOEX("%s %s %s", prot, method, uri);
		if (strcmp(prot, "HTTP/1.1") == 0)
		{
			struct Header *h = reqhdr;

			qs = strchr(uri, '?');
			if (qs)
			{
				*qs++ = '\0'; // split URI
			}
			else
			{
				qs = uri - 1; // use an empty string
			}

			while (h < reqhdr+16)
			{
				char *k, *v, *t;

				k = strtok(NULL, "\r\n: \t");
				if (!k)
				{
					break;
				}

				v = strtok(NULL, "\r\n");
				while (*v && *v == ' ')
				{
					v++;
				}

				h->name = k;
				h->value = v;
				h++;
				T_INFOEX("%s: %s", k, v);

				t = v + 1 + strlen(v);
				if (t[1] == '\r' && t[2] == '\n')
				{
					break;
				}
			}

			// bind clientfd to stdout, making it easier to write
			clientfd = clients[i];
			dup2(clientfd, STDOUT_FILENO);
			close(clientfd);

			// call router
			route();
		}

		// tidy up
		fflush(stdout);
		shutdown(STDOUT_FILENO, SHUT_WR);
		close(STDOUT_FILENO);
	}
	shutdown(clientfd, SHUT_RDWR); // All further send and recieve operations are DISABLED...
	close(clientfd);
	clients[i] = NOCLIENT;
}

void
serve_forever(void)
{
	struct sockaddr_in clientaddr;
	socklen_t addrlen;
	size_t active_slot;

	clients = mmap(NULL, sizeof(*clients)*MAX_CONNECTIONS, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	for (int i = 0; i < MAX_CONNECTIONS; i++)
	{
		clients[i] = NOCLIENT;
	}

	/* Start the server. */
	{
		struct addrinfo hints, *res, *p;

		// getaddrinfo for host
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags = AI_PASSIVE;
		if (getaddrinfo(NULL, PORT, &hints, &res) != 0)
		{
			perror("getaddrinfo() error");
			exit(1);
		}

		// socket and bind
		for (p = res; p != NULL; p = p->ai_next)
		{
			int option = 1;
			if ((listenfd = socket(p->ai_family, p->ai_socktype, 0)) < 0)
			{
				fprintf(stderr, "Socket creation issue!\n");
			}
			setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &option, sizeof(option));
			if (listenfd == -1)
			{
				continue;
			}
			if (bind(listenfd, p->ai_addr, p->ai_addrlen) == 0)
			{
				break;
			}
		}

		if (p == NULL)
		{
			perror("socket() or bind()");
			exit(1);
		}
		freeaddrinfo(res);
		if (listen(listenfd, 1000000) != 0)
		{
			perror("listen() error");
			exit(1);
		}
	}

	// Ignore SIGCHLD to avoid zombie threads
	signal(SIGCHLD, SIG_IGN);

	puts("Listening on port "PORT"...\n");
	active_slot = 0;
	for(;;)
	{
		addrlen = sizeof(clientaddr);
		clients[active_slot] = accept(listenfd, (struct sockaddr *)&clientaddr, &addrlen);

		if (clients[active_slot] < 0)
		{
			perror("accept() error");
		}
		else
		{
			if (fork() == 0)
			{
				respond(active_slot);
				exit(0);
			}
		}

		/* Find a new free slot for the next connection. */
		while (clients[active_slot] != -1)
		{
			active_slot = (active_slot + 1) % MAX_CONNECTIONS;
		}
	}
}

// get request header by name
char *
request_header(const char * const name)
{
	const struct Header *h = reqhdr;
	while (h->name)
	{
		if (strcmp(h->name, name) == 0)
		{
			return h->value;
		}
		h++;
	}
	return NULL;
}

// get all request headers
struct Header *
request_headers(void)
{
	return reqhdr;
}
