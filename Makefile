CFLAGS=-g -Wall -pipe -Werror -std=c99

OS=$(shell uname -s)

ifeq ("$(OS)", "Darwin")
	LFLAGS=-framework Cocoa
	CFLAGS+=-x objective-c
else ifeq ("$(OS)", "Linux")
	LFLAGS+=-lxcb -lxcb-image -lxcb-icccm
endif

raw_display_test: raw_display.o raw_display_test.o
	$(CC) -o $@ raw_display.o raw_display_test.o $(LFLAGS)

%.o: %.c raw_display.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm *.o raw_display_test
