CC=gcc

CFLAGS = -g -c -Wall -pedantic -D_GNU_SOURCE
#CFLAGS = -ansi -c -Wall -pedantic -D_GNU_SOURCE

all: mcast start_mcast

mcast: mcast.o recv_dbg.o
	$(CC) -o mcast mcast.o recv_dbg.o

start_mcast: start_mcast.o
	$(CC) -o start_mcast start_mcast.o

clean:
	rm *.o
	rm mcast
	rm start_mcast
