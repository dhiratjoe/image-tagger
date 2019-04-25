CC = gcc
CFLAGS = -std=c99 -O3 -Wall -Wpedantic -g

all: mkbin image-tagger-server

%: %.c
	$(CC) $(CFLAGS) -o $@ $<

.PHONY: clean mkbin

clean:
	rm -rf $(BIN_DIR)

