CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -Werror
BIN     := test_ioctl

.PHONY: all clean

all: $(BIN)

$(BIN): test_ioctl.c
	$(CC) $(CFLAGS) -o $@ $<

clean:
	rm -f $(BIN)
