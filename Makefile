OBJS = helpers.o client.o server.o
CLIENT_OUT = client
SERVER_OUT = server

client: client.cpp
	g++ -o3 -std=c++20 client.cpp helpers.cpp -o client

server: server.cpp
	g++ -o3 -std=c++20 server.cpp helpers.cpp -o server

all:
	make client
	make server

clean:
	rm -f ${OBJS} ${CLIENT_OUT} ${SERVER_OUT}
