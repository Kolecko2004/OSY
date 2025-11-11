CC = gcc
CFLAGS = -Wall -g -O2 -pthread
EXTRA_CFLAGS ?= -fsanitize=address -fno-omit-frame-pointer
LDFLAGS = -pthread

TARGET = factory
SOURCES = factory.c
OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) $(EXTRA_CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
