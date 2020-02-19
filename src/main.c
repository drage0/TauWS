#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>
#include "testpage.h"

#ifdef TAU_DEBUG_MODE
	#include <stdio.h>
	#define T_INFOEX(s, ...) fprintf(stdout, "[i] "s"\n", __VA_ARGS__)
	#define T_INFO(s)        fprintf(stdout, "[i] %s\n",  s)
	#define T_ISSUE(s)       fprintf(stderr, "[x] "s"\n");
	#define die(s) do { fputs(s, stderr); exit(EXIT_FAILURE); } while(0)
#else
	#define T_INFOEX(s, ...)
	#define T_INFO(s)
	#define T_ISSUE(s)
	#define die(s)
#endif
#define PORT "667"
#define MAX_CONNECTIONS (1<<10)
#define NOCLIENT -1

struct ClientData
{
	char *method;   /* Only GET method is implemented. */
	char *query;    /* Query string, after '?'. */
	char *uri;
	char *protocol; /* Only allowing "HTTP/1.1". */
	int file;       /* The file descriptor for writing. */
} client;
static int listenfd;
static int *clients;

/*
 * Serve the client.
 */
static void
route(void)
{
	const int method_get = (strcmp("GET", client.method) == 0);
	if (method_get)
	{
		if (strcmp("/info", client.uri) == 0)
		{
			const char data[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 28\r\n\r\nOpen test! text/plain, utf-8";
			write(client.file, data, sizeof(data));
		}
		else if (strcmp("/testpage.html", client.uri) == 0 || strcmp("/", client.uri) == 0)
		{
			const char header[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-length: "testpage_html_strlen"\r\n\r\n";
			write(client.file, header,               sizeof(header)-1);
			write(client.file, testpage_html,        testpage_html_len);
		}
	}
	else
	{
		const char data[] = "HTTP/1.1 500 Internal Server Error\r\nContent-Type: text/plain; charset=utf-8\r\nContent-Length: 41\r\n\r\nThe server has no handler to the request.";
		write(client.file, data, sizeof(data));
	}
}

/*
 * Respond to a client connection.
 */
#define MESSAGE_MAX_LENGTH 65535
static void
respond(const size_t i)
{
	ssize_t message_length;
	char *message;

	message        = malloc(MESSAGE_MAX_LENGTH);
	message_length = recv(clients[i], message, MESSAGE_MAX_LENGTH, 0);
	if (message_length < 0)
	{
		T_ISSUE("recv() error!");
	}
	else if (message_length == 0) // receive socket closed
	{
		T_ISSUE("Client disconnected upexpectedly.");
	}
	else
	{
		message[message_length] = '\0';
		client.method   = strtok(message,  " \t\r\n");
		client.uri      = strtok(NULL, " \t");
		client.protocol = strtok(NULL, " \t\r\n");

		if (strcmp(client.protocol, "HTTP/1.1") == 0)
		{
			int serve = 0;

			client.query = strchr(client.uri, '?');
			if (client.query)
			{
				*(client.query)++ = '\0'; // split URI
			}
			else
			{
				client.query = client.uri - 1; // use an empty string
			}
			T_INFOEX("protocol:%s\tmethod=%s\turi=%s\tquery=%s", client.protocol, client.method, client.uri, client.query);

			/* Find headers. */
			for (size_t i = 0; i < 16; i++)
			{
				char *key, *value, *t;

				if (!(key = strtok(NULL, "\r\n: \t")))
				{
					break;
				}
				value = strtok(NULL, "\r\n");
				while (*value && *value == ' ')
				{
					value++;
				}
				T_INFOEX("%s: %s", key, value);
				if (strncmp(key, "User-Agent", 10) == 0)
				{
					serve = !(value[0] == '-' || (value[0] == 'P' && value[1] == 'y'));
				}

				t = value+strlen(value)+1;
				if (t[1] == '\r' && t[2] == '\n')
				{
					break;
				}
			}

			client.file = clients[i];
			if (serve)
			{
				route();
			}
		}

		// tidy up
		shutdown(client.file, SHUT_WR);
		close(client.file);
	}
	shutdown(client.file, SHUT_RDWR); // All further send and recieve operations are DISABLED...
	close(client.file);
	free(message);
	clients[i] = NOCLIENT;
}

int
main(void)
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
		hints.ai_family   = AF_INET;
		hints.ai_socktype = SOCK_STREAM;
		hints.ai_flags    = AI_PASSIVE;
		if (getaddrinfo(NULL, PORT, &hints, &res) != 0)
		{
			die("getaddrinfo issue!");
		}

		// socket and bind
		for (p = res; p != NULL; p = p->ai_next)
		{
			const int option = 1;
			if ((listenfd = socket(p->ai_family, p->ai_socktype, 0)) < 0)
			{
				T_ISSUE("Socket creation issue!");
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
			die("Socket or bind issue!");
		}
		freeaddrinfo(res);
		if (listen(listenfd, 1000000) != 0)
		{
			die("Listen issue!");
		}
	}

	// Ignore SIGCHLD to avoid zombie threads
	signal(SIGCHLD, SIG_IGN);

	T_INFO("Listening on port "PORT"...\n");
	active_slot = 0;
	for(;;)
	{
		addrlen = sizeof(clientaddr);
		clients[active_slot] = accept(listenfd, (struct sockaddr *)&clientaddr, &addrlen);

		if (clients[active_slot] < 0)
		{
			T_ISSUE("accept() issue");
		}
		else
		{
			if (fork() == 0)
			{
				respond(active_slot);
				exit(EXIT_SUCCESS);
			}
		}

		/* Find a new free slot for the next connection. */
		while (clients[active_slot] != -1)
		{
			active_slot = (active_slot+1)%MAX_CONNECTIONS;
		}
	}
	return EXIT_SUCCESS;
}
