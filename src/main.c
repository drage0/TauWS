#include "httpd.h"
#include <stdlib.h>

char *page_test;
size_t page_test_length;

/*
 * Critical error.
 */
static void
die(const char * const message)
{
	fprintf(stderr, "%s\n", message);
	exit(EXIT_FAILURE);
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

/*
 * Main entry point.
 */
int
main(void)
{
	read_file("testpage.html", &page_test, &page_test_length);
	serve_forever();
	free(page_test);
	return 0;
}

/*
 * Serve the client.
 */
void
route(void)
{
	const int method_get = (strcmp("GET", method) == 0);
	if (method_get)
	{
		if (strcmp("/", uri) == 0)
		{
			printf("HTTP/1.1 200 OK\r\n\r\n");
			printf("Hello! You are using %s", request_header("User-Agent"));
		}
		else if (strcmp("/info", uri) == 0)
		{
			struct Header *h = request_headers();
			printf("HTTP/1.1 200 OK\r\n\r\n");
			printf("List of request headers:\n");

			while (h->name)
			{
				printf("%s: %s\n", h->name, h->value);
				h++;
			}
		}
		else if (strcmp("/testpage.html", uri) == 0)
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
