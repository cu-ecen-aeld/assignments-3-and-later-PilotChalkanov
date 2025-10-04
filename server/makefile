#cross-compilation prefix
CROSS_COMPILE ?=
CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -g -Wall -Werror -Wextra
TARGET ?= aesdsocket
LDFLAGS ?= -pthread
SRC = aesdsocket.c

CFLAGS += -DUSE_AESD_CHAR_DEVICE=1

default: all

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
