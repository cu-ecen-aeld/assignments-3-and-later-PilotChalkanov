#cross-compilation prefix
CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall -Werror -Wextra
TARGET = aesdsocket
SRC = aesdsocket_pthreads.c

default: all

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)
