# Makefile #
all: tftp_client tftp_server

tftp_client: tftp_client.o
	gcc tftp_client.o -o tftp_client

tftp_client.o: tftp_client.c tftp_client.h
	gcc -c -Wall tftp_client.c

tftp_server: tftp_server.o
	gcc tftp_server.o -o tftp_server

tftp_server.o: tftp_server.c tftp_server.h
	gcc -c -Wall tftp_server.c

clean:
	rm *.o tftp_client tftp_server
