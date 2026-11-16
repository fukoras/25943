CC = gcc
CFLAGS = -Wall -Wextra -O2

.PHONY: all clean

all: server client

server: server.c
	@LIBS=""; [ "`uname -s`" = "SunOS" ] && LIBS="-lsocket -lnsl"; \
	$(CC) $(CFLAGS) server.c -o server $$LIBS

client: client.c
	@LIBS=""; [ "`uname -s`" = "SunOS" ] && LIBS="-lsocket -lnsl"; \
	$(CC) $(CFLAGS) client.c -o client $$LIBS

clean:
	rm -f server client /tmp/uds_31_s.voitov_upper.sock

