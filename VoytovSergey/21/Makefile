CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic
TARGET = 21
SOURCE = 21.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean