CC ?= gcc
CFLAGS ?= -Wall -MMD -MP
LDFLAGS ?=

PROG := forkpipe
C_SOURCES := forkpipe.c
C_OBJS := $(C_SOURCES:.c=.o)
C_DEPS := $(C_OBJS:.o=.d)

NSD_OBJS := nsd_main.o nsd.o nd.o

all: $(PROG) nsd

$(PROG): $(C_OBJS)
	$(CC) $(LDFLAGS) -o $@ $^

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

nsd: $(NSD_OBJS)
	$(CC) $(LDFLAGS) -o nsd $(NSD_OBJS)

nsd_main.o: nsd_main.c nsd.h nd.h
	$(CC) $(CFLAGS) -c -o $@ $<

nsd.o: nsd.c nsd.h
	$(CC) $(CFLAGS) -c -o $@ $<

nd.o: nd.c nd.h
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(PROG) nsd *.o *.d

package: clean
	tar czf osy04.tgz Makefile *.[ch]
