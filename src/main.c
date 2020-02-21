#include <string.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <signal.h>
#include <unistd.h>
#include <netdb.h>
#include "testpage.h"

#ifdef TAU_DEBUG_MODE
	#include <stdio.h>
	#include <errno.h>
	#define T_INFOEX(s, ...)  fprintf(stdout, "[i] "s"\n", __VA_ARGS__)
	#define T_INFO(s)         fprintf(stdout, "[i] %s\n",  s)
	#define T_ISSUE(s)        fprintf(stderr, "[x] "s"\n");
	#define T_ISSUEEX(s, ...) fprintf(stdout, "[x] "s"\n", __VA_ARGS__)
	#define die(s) do { fputs(s, stderr); exit(EXIT_FAILURE); } while(0)
#else
	#define T_INFOEX(s, ...)
	#define T_INFO(s)
	#define T_ISSUE(s)
	#define T_ISSUEEX(s, ...)
	#define die(s)
#endif
#define PORT "667"
#define MAX_CONNECTIONS 8
#define NOCLIENT -1

struct ClientData
{
	char *query;    /* Query string, after '?'. */
	char *uri;
	int file;       /* The file descriptor for writing. */
};
static int *clients_slots;

/*
 * Serve the client.
 */
static void
route(const struct ClientData client)
{
	if (strcmp("/info", client.uri) == 0)
	{
		const char data[] = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: 28\r\n\r\nOpen test! text/plain, utf-8";
		write(client.file, data, sizeof(data)-1);
	}
	else if (strcmp("/testpage.html", client.uri) == 0 || strcmp("/", client.uri) == 0)
	{
		const char header[] = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-length: "testpage_html_strlen"\r\n\r\n";
		write(client.file, header,               sizeof(header)-1);
		write(client.file, testpage_html,        testpage_html_len);
	}
	else
	{
		const char data[] = "HTTP/1.0 404 Not Found\r\nContent-Type: text/plain\r\nContent-length: 22\r\n\r\n404, Nyx, Nyx...";
		write(client.file, data, sizeof(data)-1);
	}
}

/*
 * Respond to a client connection.
 */
#define MESSAGE_MAX_LENGTH 65535
static void
respond(const size_t file_index, const int file)
{
	struct ClientData client;
	ssize_t message_length;
	char *message;

	client.file    = file;
	message        = malloc(MESSAGE_MAX_LENGTH);
	message_length = recv(client.file, message, MESSAGE_MAX_LENGTH, 0);
	if (message_length < 0)
	{
		T_ISSUE("recv() error!");
		clients_slots[file_index] = -1;
	}
	else if (message_length == 0) // receive socket closed
	{
		T_ISSUE("Client disconnected upexpectedly.");
		clients_slots[file_index] = -1;
	}
	else
	{
		char *protocol;
		char *method;
		message[message_length] = '\0';
		method          = strtok(message, " \t\r\n");
		client.uri      = strtok(NULL,    " \t");
		protocol        = strtok(NULL,    " \t\r\n");

		if (strcmp(protocol, "HTTP/1.1") == 0 && strcmp("GET", method) == 0)
		{
			int serve = 0;

			client.query = strchr(client.uri, '?');
			if (client.query)
			{
				*(client.query)++ = '\0'; // split URI
			}
			else
			{
				client.query = client.uri-1; // use an empty string
			}
			T_INFOEX("CONNECTION uri=%s\tquery=%s", client.uri, client.query);

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

			if (serve)
			{
				route(client);
			}
		}
	}
	free(message);
	shutdown(client.file, SHUT_RDWR);
	clients_slots[file_index] = NOCLIENT;
}

int
main(void)
{
	size_t active_slot;
	int listenfd, *clients;
	clients       = malloc(sizeof(int)*MAX_CONNECTIONS);
	clients_slots = mmap(NULL, sizeof(*clients)*MAX_CONNECTIONS, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_SHARED, -1, 0);
	for (int i = 0; i < MAX_CONNECTIONS; i++)
	{
		clients[i]       = NOCLIENT;
		clients_slots[i] = NOCLIENT;
	}
	signal(SIGPIPE, SIG_IGN);
	signal(SIGCHLD, SIG_IGN);

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
		listenfd = -1;
		for (p = res; p != NULL; p = p->ai_next)
		{
			const int option = 1;
			if ((listenfd = socket(p->ai_family, p->ai_socktype, 0)) < 0)
			{
				die("Socket creation issue!");
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

		if (p == NULL || listenfd == -1)
		{
			die("Socket or bind issue!");
		}
		freeaddrinfo(res);
		if (listen(listenfd, 32) != 0)
		{
			die("Listen issue!");
		}
	}

	T_INFO("Listening on port "PORT"...\n");
	active_slot = 0;
	for(;;)
	{
		clients[active_slot] = accept(listenfd, NULL, NULL); // - Too many open files
		if (clients[active_slot] < 0)
		{
			T_ISSUEEX("%d accept() issue. %d %s", getpid(), clients[active_slot], strerror(errno));
			exit(EXIT_FAILURE);
		}
		else
		{
			pid_t pid = fork();
			if (pid == 0)
			{
				respond(active_slot, clients[active_slot]);
				free(clients);
				exit(EXIT_SUCCESS);
			}
			else if (pid == -1)
			{
				T_ISSUE("fork() issue");
				clients_slots[active_slot] = -1;
			}
			clients_slots[active_slot] = 1;
		}

		/* Find a new free slot for the next connection. */
		do
		{
			active_slot = (active_slot+1)%MAX_CONNECTIONS;
		} while (clients_slots[active_slot] != NOCLIENT);
		/* Was there a client connected to this slot? */
		if (clients[active_slot] != NOCLIENT)
		{
			close(clients[active_slot]);
		}
	}
	free(clients);
	return EXIT_SUCCESS;
}
