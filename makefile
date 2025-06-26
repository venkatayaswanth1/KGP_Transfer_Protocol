CC=gcc
CFLAGS=-Wall -pthread -I.

libksocket.a: ksocket.o
	ar rcs libksocket.a ksocket.o

ksocket.o: ksocket.c ksocket.h
	$(CC) $(CFLAGS) -c ksocket.c -o ksocket.o

clean:
	rm -f *.o libksocket.a
