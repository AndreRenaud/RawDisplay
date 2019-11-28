CFLAGS=-g -Wall -pipe -Werror -std=c99

LFLAGS+=-lxcb -lxcb-image -lxcb-icccm

raw_display_test: raw_display.o raw_display_test.o
	$(CC) -o $@ raw_display.o raw_display_test.o $(LFLAGS)

%.o: %.c raw_display.h
	$(CC) -c -o $@ $< $(CFLAGS)

clean:
	rm *.o raw_display_test
