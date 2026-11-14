# Build CLI userland: make  -> ./rkit
CC    ?= cc
CFLAGS ?= -O2 -Wall

all: rkit

rkit: rkit.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f rkit
