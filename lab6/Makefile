all: lab6_server lab6_client

lab6_server: server.c
	gcc -o server server.c -lpthread

lab6_client: client.c
	gcc -o client client.c 

clean:
	rm -f server
	rm -f client
