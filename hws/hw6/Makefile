CC = gcc
CFLAGS = -ffreestanding -fno-stack-protector -nostdlib -nostdinc -static -m32 -Wall -g -O2

all: hexconv

hexconv: hexconv.c
	$(CC) $(CFLAGS) hexconv.c -o hexconv

clean:
	rm -f hexconv
