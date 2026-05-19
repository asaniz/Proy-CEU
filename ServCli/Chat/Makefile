CC = gcc
CFLAGS = -Wall -Wextra -pthread
LDFLAGS = -pthread

all: serv7 cli7

serv7: serv7.c
	$(CC) $(CFLAGS) serv7.c -o serv7 $(LDFLAGS)

cli7: cli7.c
	$(CC) $(CFLAGS) cli7.c -o cli7 $(LDFLAGS)

clean:
	rm -f serv7 cli7 *.o