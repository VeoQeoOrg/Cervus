CC = gcc
CFLAGS = -Wall -Wextra -O2

TARGET = ../build

all: $(TARGET)

$(TARGET): build.c
	$(CC) $(CFLAGS) -o $@ $<
	@echo "Build script is ready at: $(TARGET)"

clean:
	rm -f $(TARGET)

.PHONY: all clean