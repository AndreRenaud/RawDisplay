CFLAGS=-g -O3 -Wall -pipe -Werror -std=c99

OS?=$(shell uname -s)

PROGRAM?=raw_display_test

ifeq ("$(OS)", "Darwin")
	LFLAGS=-framework Cocoa -lm
	# macOS uses Objective-c Cocoa code, so the .c files is really a .m
	CFLAGS+=-x objective-c
else ifeq ("$(OS)", "Linux")
	LFLAGS+=-lxcb -lxcb-image -lxcb-icccm -lm
else ifeq ("$(OS)", "Windows_NT")
	LFLAGS+=-mconsole -lgdi32
	PROGRAM=raw_display_test.exe
endif

default: $(PROGRAM)

$(PROGRAM): raw_display.o raw_display_test.o
	$(CC) -o $@ raw_display.o raw_display_test.o $(LFLAGS)

%.o: %.c raw_display.h
	$(CC) $(CFLAGS) -c -o $@ $<

docs: raw_display.h raw_display.dox
	doxygen raw_display.dox

format:
	clang-format -i raw_display.c
	clang-format -i raw_display.h

clean:
	rm *.o $(PROGRAM)

.PHONY: format clean
