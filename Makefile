CC ?= cc
OUTPUT_PATH = output
BINARY = $(OUTPUT_PATH)/arcane-server
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wpedantic

.PHONY: all clean

all: $(BINARY)

$(BINARY): server/server.c
	mkdir -p $(OUTPUT_PATH)
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -rf $(OUTPUT_PATH)