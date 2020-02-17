#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>

#define T_DOPRINT
#ifdef T_DOPRINT
	#define T_INFOEX(s, ...) printf("[i]"s"\n", __VA_ARGS__)
	#define T_INFO(s)        printf("[i]%s\n",  s)
#else
	#define T_INFOEX(s, ...)
	#define T_INFO(s)
#endif
#define PORT "667"
#define MAX_CONNECTIONS 1000
#define NOCLIENT -1

struct Header
{
	char *name, *value;
};
struct ClientData
{
	char *method;   /* Only GET method is implemented. */
	char *query;    /* Query string, after '?'. */
	char *uri;
	char *protocol; /* Only allowing "HTTP/1.1". */
} client;
static int listenfd;
static int clientfd;
static int *clients;
static char *buf;
static struct Header reqhdr[17] = {{"\0", "\0"}};

char *page_test;
size_t page_test_length;

inline static void
die(const char * const message)
{
	fputs(message, stderr);
	exit(EXIT_FAILURE);
}

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

/*
 * Serve the client.
 */
static void
route(void)
{
	const int method_get = (strcmp("GET", client.method) == 0);
	if (method_get)
	{
		if (strcmp("/", client.uri) == 0)
		{
			printf("HTTP/1.1 200 OK\r\n\r\n");
			printf("Hello! You are using %s", request_header("User-Agent"));
		}
		else if (strcmp("/info", client.uri) == 0)
		{
			struct Header *h = reqhdr;
			printf("HTTP/1.1 200 OK\r\n\r\n");
			printf("List of request headers:\n");

			while (h->name)
			{
				printf("%s: %s\n", h->name, h->value);
				h++;
			}
		}
		else if (strcmp("/testpage.html", client.uri) == 0)
		{
			printf("HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-length: %ld\r\n\r\n", page_test_length);
			puts(page_test);
		}
	}
	else
	{
		printf("HTTP/1.1 500 Internal Server Error\n\n" "The server has no handler to the request.\n");
	}
}

/*
 * Respond to a client connection.
 */
static void
respond(const size_t i)
{
	ssize_t rcvd;

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
		client.method   = strtok(buf,  " \t\r\n");
		client.uri      = strtok(NULL, " \t");
		client.protocol = strtok(NULL, " \t\r\n");

		if (strcmp(client.protocol, "HTTP/1.1") == 0)
		{
			struct Header *h = reqhdr;
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
			while (h < reqhdr+16)
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
				h->name  = key;
				h->value = value;
				h++;
				T_INFOEX("%s: %s", key, value);

				t = value+strlen(value)+1;
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
			die("getaddrinfo issue!");
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
				exit(EXIT_SUCCESS);
			}
		}

		/* Find a new free slot for the next connection. */
		while (clients[active_slot] != -1)
		{
			active_slot = (active_slot + 1) % MAX_CONNECTIONS;
		}
	}
}

static void
read_file(const char *path, char **page_content, size_t *page_length)
{
	FILE *f = fopen(path, "r");
	if (!f)
	{
		die("testpage.html cannot be read.");
	}
	fseek(f, 0L, SEEK_END);
	*page_length = ftell(f);
	fseek(f, 0L, SEEK_SET);
	*page_content = (char*)calloc(*page_length, sizeof(char));
	fread(*page_content, sizeof(char), *page_length, f);
	fclose(f);
}

int
main(void)
{
	read_file("testpage.html", &page_test, &page_test_length);
	serve_forever();
	free(page_test);
	return 0;
}
