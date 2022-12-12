OBJS = helpers.o client.o server.o
CLIENT_OUT = client
SERVER_OUT = server

all:
	make client
	make server

client: client.cpp
	g++ -o3 -std=c++2a client.cpp helpers.cpp -o client -pthread

server: server.cpp
	g++ -o3 -std=c++2a server.cpp helpers.cpp -o server -pthread

clean:
	rm -f ${OBJS} ${CLIENT_OUT} ${SERVER_OUT}
