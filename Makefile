CC = gcc

CCFLAGS = -std=c17 -pedantic -Wall -Wvla -Werror -Wno-unused-variable -D_DEFAULT_SOURCE

ALL = pas_server pas_client

all: $(ALL)

pas_server: pas_server.o game.o utils_v3.o 
	$(CC) $(CCFLAGS) -o pas_server pas_server.o game.o utils_v3.o

pas_server.o: pas_server.c messages.h
	$(CC) $(CCFLAGS) -c pas_server.c

pas_client: pas_client.o utils_v3.o
	$(CC) $(CCFLAGS) -o pas_client pas_client.o utils_v3.o

pas_client.o: pas_client.c messages.h
	$(CC) $(CCFLAGS) -c pas_client.c

game.o: game.h game.c
	$(CC) $(CFLAGS) -c game.c $(INCLUDES)

utils_v3.o: utils_v3.c utils_v3.h
	$(CC) $(CCFLAGS) -c utils_v3.c


clean:
	rm -f *.o
	rm -f $(ALL)
