CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic
TARGET = 10
SOURCE = 10.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean