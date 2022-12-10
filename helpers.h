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

#define BUFF_SIZE 1025
#define PACKET_DATA_SIZE 10
#define WINDOW_WIDTH 10
#define SLEEP_MSEC 5000

typedef struct Payload {
    bool isLast;
    char type;
    uint32_t packet_number;
    char data[PACKET_DATA_SIZE];
} Payload;
typedef struct Netstr {
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
} Netstr;

enum State {S_EXIT = 0, S_WORKING = 2, S_INITIAL = 3, S_NOT_CONNECTED = 4};

typedef struct TODO {
private:
    struct cmp_payload {
        bool operator()(Payload const& p1, Payload const& p2)
        {
            // return "true" if "p1" is ordered
            // before "p2", for example:
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

void packetmsg(int state, int *packet_number, const char* msg, TODO *todo);
int statecheck(int state, char *msg);
void statusupdate(int state, Payload *payload, int index);
void statusexit(const char* from, Payload *payload);

#endif //INC_435_THE2_SERVER_HELPERS_H
