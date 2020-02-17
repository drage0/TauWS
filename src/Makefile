CC=gcc
FLAGS= -Wall -Wextra -pedantic -O3 -s

all: server

clean:
	@rm -rf *.o
	@rm -rf server

server: main.o httpd.o
	$(CC) $(FLAGS) -o ../tauserver $^

main.o: main.c httpd.h
	$(CC) $(FLAGS) -c -o main.o main.c

httpd.o: httpd.c httpd.h
	$(CC) $(FLAGS) -c -o httpd.o httpd.c

.PHONY: clean
