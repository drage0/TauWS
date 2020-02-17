#include "httpd.h"
#include <stdlib.h>

char *page_test;
size_t page_test_length;

static void
die(const char * const message)
{
	fprintf(stderr, "%s\n", message);
	exit(EXIT_FAILURE);
}

int
main(void)
{
	size_t numbytes;
	FILE *f = fopen("testpage.html", "r");
	if (!f)
	{
		die("testpage.html cannot be read.");
	}
	fseek(f, 0L, SEEK_END);
	page_test_length = numbytes = ftell(f);
	fseek(f, 0L, SEEK_SET);
	page_test = (char*)calloc(numbytes, sizeof(char));
	fread(page_test, sizeof(char), numbytes, f);
	fclose(f);

	serve_forever();
	free(page_test);
	return 0;
}

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
			printf("HTTP/1.1 200 OK\r\n\r\n");
			//printf("List of request headers:\r\n\r\n");

			header_t *h = request_headers();

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
	else if (strcmp("/", uri) == 0 && strcmp("POST", method) == 0)
	{
		printf("HTTP/1.1 200 OK\r\n\r\n");
		/*
		printf("Wow, seems that you POSTed %d bytes. \r\n", payload_size);
		printf("Fetch the data using `payload` variable.");
		*/
	}
	else
	{
		printf("HTTP/1.1 500 Internal Server Error\n\n" "The server has no handler to the request.\n");
	}
}
