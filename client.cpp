#include "helpers.h"

// client implementation is almost the same with server, only major different that can be seen in the code is establishing connection -> initiate() function
// there is no binding in client.cpp
// client uses netstr.servaddr sockaddr_in struct to send packets and to receive packets
// differences:
// line 46
// line 53
// line 75
// line 83
// other functions are explained in server.cpp

int state = S_NOT_CONNECTED;
Netstr netstr;
TODO incoming;
TODO outgoing;
TODO waiting;

std::mutex timeout_mutex;
std::condition_variable timeout_condition;
bool timeout_flag = false;
bool timeout_first_start = true;

std::mutex exit_mutex;

char *msg_send;
char *msg_receive;

void initiate ();
void talker ();
void sender ();
void receiver ();
void printer ();

[[noreturn]] void timeout ();

int main (int argc,char **argv) {

    if ((netstr.sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr, "client: failed to create socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&netstr.servaddr, 0, sizeof(netstr.servaddr));
    netstr.servaddr.sin_family = AF_INET; // IPv4
    netstr.servaddr.sin_port = htons(atoi(argv[2]));
    netstr.servaddr.sin_addr.s_addr = inet_addr(argv[1]); // needs server address to send packets

    std::thread thread_timeout (timeout);
    std::thread thread_sender (sender);
    std::thread thread_receiver (receiver);
    std::thread thread_connector (initiate);

    // this actually does not matter anymore since only task of the connector thread is to push "S-tart" packet to outgoing queue
    if(thread_connector.joinable())
        thread_connector.join();

    std::thread thread_talker (talker);
    std::thread thread_printer (printer);

    if(thread_timeout.joinable())
        thread_timeout.join();
    if(thread_talker.joinable())
        thread_talker.join();
    if(thread_sender.joinable())
        thread_sender.join();
    if(thread_receiver.joinable())
        thread_receiver.join();
    if(thread_printer.joinable())
        thread_printer.join();

    close(netstr.sockfd);
    return 0;
}

// to establish connection client begin with sending "S-tart" packets
// until getting any "A-ck" packet, client continuously sends this packet
// it is receiver and timeout threads responsibility to ensure connection
void initiate () {
    fprintf(stderr, "------CONNECTOR--START-------\n");
    Payload payload;

    payload.type = 'S';
    // to have the highest priority at outgoing priority queue
    // but actually it does not matter since in the beginning server want packet number of 1
    // and in any case of "A-ck" response "S-tart" packet dequeues
    payload.packet_number = 0;
    outgoing.push(payload);

    fprintf(stderr, "------CONNECTOR--EXIT--------\n");
}

[[noreturn]] void timeout () {
    fprintf(stderr, "--------TIMEOUT--START--------\n");
    std::unique_lock<std::mutex> lock(timeout_mutex);

    while(true) {
        timeout_condition.wait_for(lock,
                                   std::chrono::milliseconds (SLEEP_MSEC),
                                   []() { return timeout_flag; });

        if (timeout_first_start) {
            fprintf(stderr, "----TIMEOUT--NOT--STARTED----\n");
            timeout_flag = false;
        } else if (timeout_flag) {
            fprintf(stderr, "---------NO--TIMEOUT---------\n");
            timeout_flag = false;
        } else {
            fprintf(stderr, "-----------TIMEOUT-----------\n");
            while (!waiting.empty()) {
                outgoing.push(waiting.pop());
            }
            outgoing.resetwindowindex();
        }
    }
}

void talker() {
    fprintf(stderr, "--------TALKER--START--------\n");
    int msg_len;
    int packet_number = 1;

    msg_send = (char*) malloc(BUFF_SIZE * sizeof(char));

    while (state != S_EXIT) {
        getline(&msg_send, reinterpret_cast<size_t *>(&msg_len), stdin);
        state = statecheck(state, msg_send);

        if (state != S_EXIT) {
            packetmsg(&packet_number, msg_send, &outgoing);
        } else {
            Payload payload;
            payload.type = 'E';
            payload.packet_number = 0;

            while(!outgoing.empty())
                outgoing.pop();
            while(!waiting.empty())
                waiting.pop();
            outgoing.push(payload);
        }
    }

    fprintf(stderr, "--------TALKER--EXIT---------\n");
}

void sender() {
    fprintf(stderr, "--------SENDER--START--------\n");
    Payload payload;

    while(true) {
        if(outgoing.getwindowendindex() != 0 && !outgoing.empty()) {
            payload = outgoing.pop();
            if (sendto(netstr.sockfd, (Payload *) &payload, sizeof(Payload), 0,
                       (const struct sockaddr *) &netstr.servaddr, sizeof(netstr.servaddr)) == -1) {
                fprintf(stderr, "sender: failed to send\n");
                exit(EXIT_FAILURE);
            }
            if (timeout_first_start) {
                std::lock_guard<std::mutex> lock(timeout_mutex);
                timeout_first_start = false;
                timeout_flag = true;
                timeout_condition.notify_one();
            }
            waiting.push(payload);
            statusupdate(state, &payload, outgoing.decrementwindowendindex());
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
}

void receiver () {
    fprintf(stderr, "-------RECEIVER--START-------\n");

    int msg_len;
    int available_window = outgoing.getwindowendindex();
    int incoming_index = 1;
    int count = 0;

    Payload payload, payload_ack;
    payload_ack.type = 'A';


    while (state != S_EXIT) {
        if (recvfrom(netstr.sockfd, (Payload *) &payload, sizeof(Payload), MSG_WAITALL,
                    (struct sockaddr *) &netstr.servaddr, reinterpret_cast<socklen_t *>(&msg_len)) == -1) {
            fprintf(stderr, "listener: failed to receive\n");
            exit(EXIT_FAILURE);
        }
        switch (payload.type) {
            case 'M':
                payload_ack.packet_number = incoming_index;
                sendto(netstr.sockfd, (Payload *) &payload_ack, sizeof(Payload), 0,
                       (const struct sockaddr *) &netstr.servaddr, sizeof(netstr.servaddr));
                if(payload.packet_number == incoming_index) {
                    incoming.push(payload);
                    incoming_index++;
                }
            case 'A':
                if (!waiting.empty() && payload.packet_number > waiting.top().packet_number) {
                    {
                        std::lock_guard<std::mutex> lock(timeout_mutex);
                        timeout_flag = true;
                        timeout_condition.notify_one();
                    }
                    available_window = outgoing.incrementwindowendindex();
                    waiting.pop();
                }
                statusupdate(state, &payload, available_window);
                break;
            case 'E':
                state = S_EXIT;
                while(count != 10) {
                    if (sendto(netstr.sockfd, (Payload *) &payload, sizeof(Payload), 0,
                               (const struct sockaddr *) &netstr.servaddr, sizeof(netstr.servaddr)) == -1) {
                        fprintf(stderr, "sender: failed to send\n");
                        exit(EXIT_FAILURE);
                    }
                    count++;
                }
                break;
            case 'S':
                {
                    std::lock_guard<std::mutex> lock(timeout_mutex);
                    timeout_first_start = true;
                    timeout_flag = true;
                    timeout_condition.notify_one();
                }
                available_window = outgoing.incrementwindowendindex();
                waiting.pop();
                state = S_INITIAL;
                statusupdate(state, &payload, available_window);
                break;
        }
    }

    fprintf(stderr, "-------RECEIVER---EXIT-------\n");
    std::unique_lock<std::mutex> lock(exit_mutex);
    free(msg_send);
    free(msg_receive);
    close(netstr.sockfd);
    exit(0);
}

void printer() {
    fprintf(stderr, "--------PRINTER-START--------\n");
    int packet_number = 0;
    Payload payload;

    msg_receive = (char*) malloc(BUFF_SIZE * sizeof(char));

    while(state != S_EXIT) {
        if(!incoming.empty()) {
            payload = incoming.pop();
            for(int i = 0; i < PACKET_DATA_SIZE; i++) {
                msg_receive[packet_number * PACKET_DATA_SIZE + i] = payload.data[i];
                if(payload.data[i] == '\n') {
                    msg_receive[packet_number * PACKET_DATA_SIZE + i + 1] = '\0';
                    break;
                }
            }
            packet_number++;
            if(payload.isLast == true) {
                fprintf(stdout, "%s", msg_receive);
                packet_number = 0;
            }
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }

    fprintf(stderr, "--------PRINTER--EXIT--------\n");
}
