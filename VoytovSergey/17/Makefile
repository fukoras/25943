CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic
TARGET = 17
SOURCE = 17.c

all: $(TARGET)

$(TARGET): $(SOURCE)
	$(CC) $(CFLAGS) $(SOURCE) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean