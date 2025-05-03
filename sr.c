#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "emulator.h"
#include "sr.h"

#define RTT 16.0
#define WINDOWSIZE 6
#define SEQSPACE 12
#define NOTINUSE (-1)

/* Sender side structures */
struct pkt sender_buffer[SEQSPACE];
bool sender_ack_received[SEQSPACE];
double sender_send_time[SEQSPACE];
int base;
int nextseqnum;

/* Receiver side structures */
struct pkt receiver_buffer[SEQSPACE];
bool receiver_buffer_filled[SEQSPACE];
int expectedseqnum;

/* Timer state */
bool timer_running = false;
double current_time = 0.0;

int compute_checksum(struct pkt packet) {
    int checksum = packet.seqnum + packet.acknum;
    int i;
    for (i = 0; i < 20; i++) {
        checksum += (unsigned char)packet.payload[i];
    }
    return checksum;
}

bool is_corrupted(struct pkt packet) {
    return compute_checksum(packet) != packet.checksum;
}

bool is_seqnum_in_window(int seqnum, int base) {
    return ((seqnum >= base && seqnum < base + WINDOWSIZE) ||
           (base + WINDOWSIZE >= SEQSPACE && seqnum < (base + WINDOWSIZE) % SEQSPACE));
}

void restart_timer() {
    if (timer_running) stoptimer(A);
    starttimer(A, RTT);
    timer_running = true;
}

void A_output(struct msg message) {
    struct pkt packet;

    if (!is_seqnum_in_window(nextseqnum, base)) {
        if (TRACE > 0) printf("----A: New message arrives, window is full\n");
        return;
    }

    packet.seqnum = nextseqnum;
    packet.acknum = NOTINUSE;
    memcpy(packet.payload, message.data, 20);
    packet.checksum = compute_checksum(packet);

    sender_buffer[nextseqnum] = packet;
    sender_ack_received[nextseqnum] = false;
    sender_send_time[nextseqnum] = current_time;

    tolayer3(A, packet);
    if (TRACE > 0) printf("----A: Sending packet %d to layer 3\n", packet.seqnum);

    if (!timer_running) restart_timer();

    nextseqnum = (nextseqnum + 1) % SEQSPACE;
}

void A_input(struct pkt packet) {
    int i;
    bool outstanding = false;

    if (is_corrupted(packet)) {
        if (TRACE > 0) printf("----A: Corrupted ACK received, ignored\n");
        return;
    }

    if (TRACE > 0) printf("----A: ACK %d received\n", packet.acknum);
    sender_ack_received[packet.acknum] = true;

    while (sender_ack_received[base]) {
        sender_ack_received[base] = false;
        base = (base + 1) % SEQSPACE;
    }

    for (i = 0; i < WINDOWSIZE; i++) {
        int idx = (base + i) % SEQSPACE;
        if (!sender_ack_received[idx] && is_seqnum_in_window(idx, base)) {
            outstanding = true;
            break;
        }
    }

    if (!outstanding) {
        stoptimer(A);
        timer_running = false;
    } else {
        restart_timer();
    }
}

void A_timerinterrupt(void) {
    int i;
    if (TRACE > 0) printf("----A: Timer interrupt, checking packets for retransmission\n");

    for (i = 0; i < WINDOWSIZE; i++) {
        int idx = (base + i) % SEQSPACE;
        if (!sender_ack_received[idx] && is_seqnum_in_window(idx, base)) {
            tolayer3(A, sender_buffer[idx]);
            if (TRACE > 0) printf("----A: Resending packet %d\n", idx);
            sender_send_time[idx] = current_time;
        }
    }

    restart_timer();
}

void A_init(void) {
    int i;
    base = 0;
    nextseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        sender_ack_received[i] = false;
        sender_send_time[i] = 0.0;
    }
    timer_running = false;
}

void B_input(struct pkt packet) {
    struct pkt ack_pkt;
    if (is_corrupted(packet)) {
        if (TRACE > 0) printf("----B: Corrupted packet received, sending duplicate ACK\n");
        ack_pkt.seqnum = NOTINUSE;
        ack_pkt.acknum = (expectedseqnum + SEQSPACE - 1) % SEQSPACE;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = compute_checksum(ack_pkt);
        tolayer3(B, ack_pkt);
        return;
    }

    if (TRACE > 0) printf("----B: Packet %d received\n", packet.seqnum);

    if (!receiver_buffer_filled[packet.seqnum]) {
        receiver_buffer[packet.seqnum] = packet;
        receiver_buffer_filled[packet.seqnum] = true;

        ack_pkt.seqnum = NOTINUSE;
        ack_pkt.acknum = packet.seqnum;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = compute_checksum(ack_pkt);
        tolayer3(B, ack_pkt);

        if (TRACE > 0) printf("----B: Sent ACK %d\n", packet.seqnum);

        while (receiver_buffer_filled[expectedseqnum]) {
            tolayer5(B, receiver_buffer[expectedseqnum].payload);
            receiver_buffer_filled[expectedseqnum] = false;
            expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
        }
    } else {
        if (TRACE > 0) printf("----B: Duplicate packet %d received\n", packet.seqnum);
        ack_pkt.seqnum = NOTINUSE;
        ack_pkt.acknum = packet.seqnum;
        memset(ack_pkt.payload, 0, 20);
        ack_pkt.checksum = compute_checksum(ack_pkt);
        tolayer3(B, ack_pkt);
    }
}

void B_init(void) {
    int i;
    expectedseqnum = 0;
    for (i = 0; i < SEQSPACE; i++) {
        receiver_buffer_filled[i] = false;
    }
}

void B_output(struct msg message) {}
void B_timerinterrupt(void) {}
