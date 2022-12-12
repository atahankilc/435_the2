#ifndef INC_435_THE2_SERVER_HELPERS_H
#define INC_435_THE2_SERVER_HELPERS_H

#include <cstdio>
#include <cstdlib>
#include <unistd.h>
#include <cstring>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>

// buffer size is 1024 + 1
// +1 added since I prefer to add \0 after every line
#define BUFF_SIZE 1025
#define PACKET_DATA_SIZE 10
#define WINDOW_WIDTH 20
#define SLEEP_MSEC 300

// 1 byte to indicate if packet contains "\n"
// 1 byte for type of the packet, can be "M-essage", "A-ck", "S-tart", "E-nd"
// 4 byte for numbering packets, rather hugh value due to the fact that I don't want to mind it
// 16 - 6 = 10 byte of data can be sent in each packet
typedef struct Payload {
    bool isLast;
    char type;
    uint32_t packet_number;
    char data[PACKET_DATA_SIZE];
} Payload;

// generalized structure for all the networking things
// cliaddr is for server, client never uses it
typedef struct Netstr {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
} Netstr;

// to determine which state client/server is in
// S_NOT_CONNECTED state is for client since it is client's duty to correctly establish connection with server
enum State {S_EXIT = 0, S_WORKING = 2, S_INITIAL = 3, S_NOT_CONNECTED = 4};

// general packet queuing structure
// it prioritizes packets with smaller packet_number.
// it limits the number of packets to be sent according to the window width
typedef struct TODO {
private:
    struct cmp_payload {
        bool operator()(Payload const& p1, Payload const& p2){
            return p1.packet_number > p2.packet_number;
        }
    };
    std::priority_queue<Payload, std::vector<Payload>, cmp_payload> queue;
    int window_end_index = WINDOW_WIDTH;
    std::mutex mutex;
public:
    void push(Payload p) {
        queue.push(p);
    }
    Payload top() {
        const std::lock_guard<std::mutex> lock(mutex);
        return queue.top();
    }
    Payload pop() {
        const std::lock_guard<std::mutex> lock(mutex);
        Payload p = queue.top();
        queue.pop();
        return p;
    }
    bool empty() {
        const std::lock_guard<std::mutex> lock(mutex);
        return queue.empty();
    }
    int getwindowendindex() {
        const std::lock_guard<std::mutex> lock(mutex);
        return window_end_index;
    }
    int decrementwindowendindex() {
        const std::lock_guard<std::mutex> lock(mutex);
        return --window_end_index;
    }
    int incrementwindowendindex() {
        const std::lock_guard<std::mutex> lock(mutex);
        return ++window_end_index;
    }
    int resetwindowindex() {
        const std::lock_guard<std::mutex> lock(mutex);
        window_end_index = WINDOW_WIDTH;
        return window_end_index;
    }
} TODO;

// packets input strings and pushes to the queue
void packetmsg(int *packet_number, const char* msg, TODO *todo);

// changes state according to the user input
int statecheck(int state, char *msg);

// statusupdate and statusexit is for debugging purposes
// prints overall information about what is going on to the stderr
void statusupdate(int state, Payload *payload, int index);
// indicates which thread is exiting
// it is not being used in the current implementation
void statusexit(const char* from, Payload *payload);

#endif //INC_435_THE2_SERVER_HELPERS_H
