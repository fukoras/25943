CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = 25
SRC = 25.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(TARGET)

clean:
	rm -f $(TARGET)
