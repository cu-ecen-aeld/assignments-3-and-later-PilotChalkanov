CROSS_COMPILE ?=
CC = $(CROSS_COMPILE)gcc
CFLAGS = -g -Wall
TARGET = writer
SRC = writer.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -g -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)