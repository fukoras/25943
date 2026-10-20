CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic
TARGET = 9
SOURCE = 9.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean