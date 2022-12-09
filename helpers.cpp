#include "helpers.h"

void packetmsg(int state, int *packet_number, const char* msg, TODO *todo) {
    bool last_packet = false;
    int packet_count = 0;
    Payload payload;
    while (!last_packet) {
        payload.isLast = false;
        payload.type = 'M';
        payload.packet_number = (*packet_number)++;
        for (int i = 0; i < PACKET_DATA_SIZE; i++) {
            payload.data[i] = msg[packet_count * PACKET_DATA_SIZE + i];
            if (msg[packet_count * PACKET_DATA_SIZE + i] == '\n') {
                if (i < 9) {
                    payload.data[i + 1] = '\0';
                }
                payload.isLast = true;
                last_packet = true;
                break;
            }
        }
        packet_count++;
        todo->push(payload);
    }
}

int statecheck(int state, char *msg) {
    if(strcmp(msg, "\n") == 0) {
        state--;
    } else {
        state = S_WORKING;
    }
    return state;
}

void statusupdate(int state, Payload *payload, int index) {
    fprintf(stderr, "--------------%c--------------\n", payload->type);
    fprintf(stderr, "state: %i\n", state);
    fprintf(stderr, "index: %i\n", index);
    fprintf(stderr, "Payload Number: %i\n", payload->packet_number);
    if(payload->isLast == true && payload->type == 'M')
        fprintf(stderr, "Payload Data: %s", payload->data);
    else if(payload->type == 'M')
        fprintf(stderr, "Payload Data: %s\n", payload->data);
    fprintf(stderr, "-----------------------------\n");
}

void statusstart(const char* from, Payload *payload) {
    fprintf(stderr, "--------------S--------------\n");
    fprintf(stderr, "from: %s\n", from);
    fprintf(stderr, "Payload Type: %c\n", payload->type);
    fprintf(stderr, "-----------------------------\n");
}

void statusexit(const char* from, Payload *payload) {
    fprintf(stderr, "--------------E--------------\n");
    fprintf(stderr, "from: %s\n", from);
    fprintf(stderr, "Payload Type: %c\n", payload->type);
    fprintf(stderr, "Payload Number: %i\n", payload->packet_number);
    fprintf(stderr, "-----------------------------\n");
}

