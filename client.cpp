#include "helpers.h"

int state = S_NOT_CONNECTED;
Netstr netstr;
TODO incoming;
TODO outgoing;
TODO waiting;

std::mutex timeout_mutex;
std::condition_variable timeout_condition;
bool timeout_flag = false;
bool timeout_first_start = true;

char *msg_send;
char *msg_receive;

void initiate ();
void talker ();
void sender ();
void receiver ();
void printer ();
void timeout ();

int main (int argc,char **argv) {

    if ((netstr.sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr, "client: failed to create socket\n");
        exit(EXIT_FAILURE);
    }

    memset(&netstr.servaddr, 0, sizeof(netstr.servaddr));
    netstr.servaddr.sin_family = AF_INET;
    netstr.servaddr.sin_port = htons(atoi(argv[2]));
    netstr.servaddr.sin_addr.s_addr = inet_addr(argv[1]);

    std::thread thread_timeout (timeout);
    std::thread thread_sender (sender);
    std::thread thread_receiver (receiver);
    std::thread thread_connector (initiate);

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

void initiate () {
    fprintf(stderr, "------CONNECTOR--START-------\n");
    Payload payload;

    payload.type = 'S';
    payload.packet_number = 0;
    outgoing.push(payload);

    fprintf(stderr, "------CONNECTOR--EXIT--------\n");
}

void timeout () {
    fprintf(stderr, "--------TIMEOUT--START--------\n");
    std::unique_lock<std::mutex> lock(timeout_mutex);

    while(state != S_EXIT) {
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

    fprintf(stderr, "--------TIMEOUT---EXIT--------\n");
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
            //Payload payload;
            //payload.type = 'E';
            //payload.packet_number = 0;
            //outgoing.push(payload);
            //statusexit("talker", &payload);
        }
    }

    free(msg_send);
    free(msg_receive);
    fprintf(stderr, "--------TALKER--EXIT---------\n");
    exit(0);
}

void sender() {
    fprintf(stderr, "--------SENDER--START--------\n");
    Payload payload;

    while(payload.type != 'E') {
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

    fprintf(stderr, "--------SENDER--EXIT---------\n");
}

void receiver () {
    fprintf(stderr, "-------RECEIVER--START-------\n");

    int msg_len;
    int available_window = outgoing.getwindowendindex();
    int incoming_index = 1;

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
                //sendto(netstr.sockfd, (Payload *) &payload, sizeof(Payload), 0,
                //       (const struct sockaddr *) &netstr.servaddr, sizeof(netstr.servaddr));
                //state = S_EXIT;
                //statusexit("listener", &payload);
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
