CC=gcc
CFLAGS=-I.

client: client.c requests.c helpers.c buffer.c parson.c
	$(CC) -o client client.c requests.c parson.c helpers.c buffer.c -Wall

run: client
	./client

clean:
	rm -f *.o client
