#include "helpers.h"

// explained in helpers.h
int state = S_INITIAL;
// explained in helpers.h
Netstr netstr;
// queue for incoming packets
TODO incoming;
// queue for outgoing packets
TODO outgoing;
// queue for packets that send but did not "A-ck" yet
// when timeout, all the que pushed back to outgoing queue
// since it is a priority queue, outgoing resends all the not "A-ck" packets
TODO waiting;

// for timeout thread functionality
std::mutex timeout_mutex;
std::condition_variable timeout_condition;
bool timeout_flag = false;
bool timeout_first_start = true;

// for freeing allocated memory
// talker and receiver can free memory, and so I used a mutex
std::mutex exit_mutex;

// for getting user input and displaying incoming messages
char *msg_send;
char *msg_receive;

void initiate ();
void talker ();
void sender ();
void receiver ();
void printer ();
void timeout ();

int main(int argc,char **argv) {

    // socket creation
    if ((netstr.sockfd = socket(AF_INET, SOCK_DGRAM, 0)) == -1) {
        fprintf(stderr, "server: failed to create socket\n");
        exit(EXIT_FAILURE);
    }

    // sockaddr_in struct adjustments for binding (servaddr) and sending, receiving packets (cliaddr)
    memset(&netstr.servaddr, 0, sizeof(netstr.servaddr));
    memset(&netstr.cliaddr, 0, sizeof(netstr.cliaddr));
    netstr.servaddr.sin_family    = AF_INET; // IPv4
    netstr.servaddr.sin_addr.s_addr = INADDR_ANY; // does not matter
    netstr.servaddr.sin_port = htons(atoi(argv[1]));

    // binding to the indicated port
    if (bind(netstr.sockfd, (const struct sockaddr *)&netstr.servaddr, sizeof(netstr.servaddr)) == -1) {
        fprintf(stderr, "server: failed to bind\n");
        close(netstr.sockfd);
        exit(EXIT_FAILURE);
    }

    std::thread thread_timeout (timeout);
    std::thread thread_sender (sender);
    std::thread thread_receiver (receiver);
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

// if timeout thread is wakeup by another thread -> no timeout happened, sleep again
// otherwise -> expected "A-ck" packet for corresponding "M-essage" packet did not receive, push every packet in waiting queue to the outgoing queue
// actual task of timeout thread begins when the first packet is sent, until then do nothing when wakeup
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

// gets user input and divide into 16 byte packet so that sender thread can send them
// packet structure (struct Payload) is explained in helpers.h
// after the state check, if state is S_EXIT, send 5 consecutive "E-nd" packet and kill the whole server processes
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
            int count = 0;
            int cliaddr_len;
            Payload payload;

            cliaddr_len = sizeof(netstr.cliaddr);
            payload.type = 'E';
            payload.packet_number = 0;
            while(count != 5) {
                if (sendto(netstr.sockfd, (Payload *) &payload, sizeof(Payload), 0,
                           (const struct sockaddr *) &netstr.cliaddr, cliaddr_len) == -1) {
                    fprintf(stderr, "sender: failed to send\n");
                    exit(EXIT_FAILURE);
                }
                count++;
            }
        }
    }

    fprintf(stderr, "--------TALKER--EXIT---------\n");
    std::unique_lock<std::mutex> lock(exit_mutex);
    free(msg_send);
    free(msg_receive);
    exit(0);
}

// sends packets in outgoing queue
// if outgoing queue is not empty and outgoing queue window is higher than 0, send packet with the smallest packet_number
// if it is the first packet to be sent, start timeout thread process
// push sent packet to the waiting queue so that when timeout happens packets can be sent again
// if current state is S_EXIT, thread exits from the function
void sender() {
    fprintf(stderr, "--------SENDER--START--------\n");
    Payload payload;
    int cliaddr_len;

    cliaddr_len = sizeof(netstr.cliaddr);

    while(state != S_EXIT) {
        if(outgoing.getwindowendindex() != 0 && !outgoing.empty()) {
            payload = outgoing.pop();
            if (sendto(netstr.sockfd, (Payload *) &payload, sizeof(Payload), 0,
                      (const struct sockaddr *) &netstr.cliaddr, cliaddr_len) == -1) {
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

// receives packets from client
// if packet type is "M-essage", send the packet_number of the receiver thread is waiting for, regardless of the number of the incoming package
// if the incoming packet's packet_number matches the expected packet_number, increase the expected packet_number by one
// and push packet to incoming queue so that packets are pushed to incoming queue only once
// if packet type is "A-ck" and packet's packet_number is higher than waiting queue's top packet, dequeue a packet from waiting queue (packet with the smallest packet_number)
// since receiving "A-ck" with higher packet_number means that this package has already arrived at the client and is waiting for a higher numbered package.
// if packet tye is "E-nd", change state to S_EXIT, free allocated memory and kill the whole server processes
// if packet type is "S-tart", just resent packet (this does not matter for both client/server
// since receiving a packet is enough for server to have address of client and receiving any packet dequeues "S-tart" packet in client)
void receiver () {
    fprintf(stderr, "-------RECEIVER--START-------\n");

    int cliaddr_len;
    int available_window = outgoing.getwindowendindex();
    int incoming_index = 1;

    Payload payload, payload_ack;
    payload_ack.type = 'A';

    cliaddr_len = sizeof(netstr.cliaddr);

    while (state != S_EXIT) {
        if (recvfrom(netstr.sockfd, (Payload *) &payload, sizeof(Payload), MSG_WAITALL,
                    (struct sockaddr *) &netstr.cliaddr, reinterpret_cast<socklen_t *>(&cliaddr_len)) == -1) {
            fprintf(stderr, "listener: failed to receive\n");
            exit(EXIT_FAILURE);
        }
        switch (payload.type) {
            case 'M':
                payload_ack.packet_number = incoming_index;
                sendto(netstr.sockfd, (Payload *) &payload_ack, sizeof(Payload), 0,
                       (const struct sockaddr *) &netstr.cliaddr, cliaddr_len);
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
                //       (const struct sockaddr *) &netstr.cliaddr, cliaddr_len);
                //statusexit("listener", &payload);
                state = S_EXIT;
                break;
            case 'S':
                sendto(netstr.sockfd, (Payload *) &payload, sizeof(Payload), 0,
                       (const struct sockaddr *) &netstr.cliaddr, cliaddr_len);
                statusupdate(state, &payload, available_window);
        }
    }

    fprintf(stderr, "-------RECEIVER---EXIT-------\n");
    std::unique_lock<std::mutex> lock(exit_mutex);
    free(msg_send);
    free(msg_receive);
    exit(0);
}

// prints packets from incoming queue
// waits until receiving "\n" (payload.isLast == true) for corresponding message
// when "\n" received print whole message to the user
// thread exits when state is S_EXIT
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
