CC = gcc
CFLAGS = -Wall -Wextra -O2

.PHONY: all clean

all: server30 client30

server30: server.c
	@LIBS=""; [ "`uname -s`" = "SunOS" ] && LIBS="-lsocket -lnsl"; $(CC) $(CFLAGS) server.c -o server30 $$LIBS

client30: client.c
	@LIBS=""; [ "`uname -s`" = "SunOS" ] && LIBS="-lsocket -lnsl"; $(CC) $(CFLAGS) client.c -o client30 $$LIBS

clean:
	rm -f server30 client30 /tmp/uds_30_s.voitov_upper.sock
