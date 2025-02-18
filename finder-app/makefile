CC = aarch64-none-linux-gnu-gcc
CFLAGS = -g -Wall
TARGET = writer.o
SRC = writer.c

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -c -o $(TARGET) $(SRC)

clean:
	rm -f $(TARGET)