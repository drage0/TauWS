CC=gcc
FLAGS= -Wall -Wextra -pedantic -O3 -s

all: embed
	$(CC) $(FLAGS) main.c -o ../tauserver

embed: testpage.h

testpage.h: testpage.html
	xxd -i testpage.html > $@

clean:
	-rm -rf *.o
	-rm -rf server
	-rm testpage.h

.PHONY: clean
