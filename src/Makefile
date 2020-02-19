CC=gcc
FLAGS= -Wall -Wextra -pedantic -O3 -s

all: embed
	$(CC) $(FLAGS) main.c -o ../tauserver

debug: FLAGS+=-DTAU_DEBUG_MODE
debug: all

embed: testpage.h

# Convert the html page into a C char array.
# The last line is duplicated and transformed into a char array representing
# the length of the array (but not as an integer).
# This effectively helps the server not convert integer value to a char array
# during run-time.
testpage.h: testpage.html
	xxd -i testpage.html > $@
	tail -n 1 $@ | sed 's/unsigned int /#define /g; s/_len/_strlen/g; s/= //g; s/\([0-9]\+\)/"\1"/g; s/;//g' >> $@

clean:
	-rm -rf *.o
	-rm -rf server
	-rm testpage.h

.PHONY: clean
