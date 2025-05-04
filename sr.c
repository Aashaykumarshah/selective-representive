#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 7
#define NOTINUSE (-1)

int ComputeChecksum(struct pkt packet)
{
  int checksum = 0;
  int i;

  checksum = packet.seqnum;
  checksum += packet.acknum;
  for (i = 0; i < 20; i++)
    checksum += (int)(packet.payload[i]);

  return checksum;
}

bool IsCorrupted(struct pkt packet)
{
  return packet.checksum != ComputeChecksum(packet);
}

/********* Sender (A) variables and functions ************/

static struct pkt buffer[WINDOWSIZE];
static int windowfirst, windowlast;
static int windowcount;
static int A_nextseqnum;
extern int packets_resent;
static bool acked[WINDOWSIZE];

void A_output(struct msg message)
{
  struct pkt sendpkt;
  int i;
  

  if (windowcount < WINDOWSIZE) {
    if (TRACE > 1)
      printf("----A: New message arrives, send window is not full, send new messge to layer3!\n");

    sendpkt.seqnum = A_nextseqnum;
    sendpkt.acknum = NOTINUSE;
    for (i = 0; i < 20; i++)
      sendpkt.payload[i] = message.data[i];
    sendpkt.checksum = ComputeChecksum(sendpkt);

    windowlast = (windowfirst + windowcount) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
    acked[windowlast] = false;
    windowcount++;

    if (TRACE > 0)
      printf("Sending packet %d to layer 3\n", sendpkt.seqnum);
    tolayer3(A, sendpkt);

    if (windowcount == 1)
      starttimer(A, RTT);

    A_nextseqnum = (A_nextseqnum + 1) % SEQSPACE;
  } else {
    if (TRACE > 0)
      printf("----A: New message arrives, send window is full\n");
    window_full++;
  }
}


void A_input(struct pkt packet)
{
  int i;
  int index = windowfirst;

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      for (i = 0; i < windowcount; i++) {
        if (buffer[index].seqnum == packet.acknum) {
          if (!acked[index]) {
            if (TRACE > 0)
              printf("----A: ACK %d is not a duplicate\n", packet.acknum);
            acked[index] = true;
            new_ACKs++;
          }

          /* Slide window forward while base is ACKed */
          while (windowcount > 0 && acked[windowfirst]) {
            acked[windowfirst] = false;
            windowfirst = (windowfirst + 1) % WINDOWSIZE;
            windowcount--;
          }

          stoptimer(A);
          if (windowcount > 0)
            starttimer(A, RTT);
          return;
        }
        index = (index + 1) % WINDOWSIZE;
      }

      if (TRACE > 0)
        printf("----A: ACK %d not found in window, possibly already processed\n", packet.acknum);
    }
  } else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}


void A_timerinterrupt(void)
{
  int i;
  int index = windowfirst;

  if (windowcount == 0) {
    if (TRACE > 0)
      printf("----A: Timer interrupt but window is empty, nothing to resend.\n");
    return;
  }

  if (TRACE > 0)
    printf("----A: Timeout occurred, retransmitting all packets in window\n");

  for (i = 0; i < windowcount; i++) {
    if (!acked[index]) {
      if (TRACE > 1)
        printf("----A: Resending packet %d\n", buffer[index].seqnum);
      tolayer3(A, buffer[index]);
      packets_resent++;
    }
    index = (index + 1) % WINDOWSIZE;
  }
  starttimer(A, RTT);

  if (TRACE > 1)
    printf("----A: Timer restarted after retransmission\n");
}



void A_init(void)
{
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  for (i = 0; i < WINDOWSIZE; i++) {
    acked[i] = false;
  }
}
/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum;
static int B_nextseqnum;
#define RCV_BUFFER_SIZE SEQSPACE
static struct pkt recv_buffer[RCV_BUFFER_SIZE];
static bool recv_flags[RCV_BUFFER_SIZE];
void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  if (!IsCorrupted(packet)) {
    int seq = packet.seqnum;

    if (!recv_flags[seq]) {
      recv_buffer[seq] = packet;
      recv_flags[seq] = true;
    }

    /* Deliver in-order packets to application */
    while (recv_flags[expectedseqnum]) {
      tolayer5(B, recv_buffer[expectedseqnum].payload);
      recv_flags[expectedseqnum] = false;
      packets_received++;
      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
    }

    sendpkt.acknum = seq;  /* always ACK the received packet */
  } else {
    if (TRACE > 0)
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");

    if (expectedseqnum == 0)
      sendpkt.acknum = SEQSPACE - 1;
    else
      sendpkt.acknum = expectedseqnum - 1;
  }

  sendpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;

  for (i = 0; i < 20; i++)
    sendpkt.payload[i] = '0';

  sendpkt.checksum = ComputeChecksum(sendpkt);
  tolayer3(B, sendpkt);
}


void B_init(void)
{
  int i;
  expectedseqnum = 0;
  B_nextseqnum = 1;
  for (i = 0; i < RCV_BUFFER_SIZE; i++) {
    recv_flags[i] = false;
  }
}

void B_output(struct msg message)
{
}

void B_timerinterrupt(void)
{
}
