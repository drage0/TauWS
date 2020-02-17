#pragma once

#include <string.h>
#include <stdio.h>
#define T_DOPRINT
#ifdef T_DOPRINT
#define T_INFOEX(s, ...) printf("[i]"s"\n", __VA_ARGS__)
#define T_INFO(s)        printf("[i]%s\n",  s)
#else
#define T_INFOEX(s, ...)
#define T_INFO(s)
#endif

// Server control functions

void serve_forever(void);

// Client request

char *method;  /* "GET" or "POST" */
char *uri;     /* "/index.html" things before '?' */
char *qs;      /* "a=1&b=2"     things after  '?' */
char *prot;    /* "HTTP/1.1" */

char *request_header(const char *name);

struct Header
{
	char *name, *value;
};
static struct Header reqhdr[17] = {{"\0", "\0"}};
struct Header *request_headers(void);

// user shall implement this function
extern void route(void);
