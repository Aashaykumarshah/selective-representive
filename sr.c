#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include "emulator.h"
#include "gbn.h"

#define RTT  16.0
#define WINDOWSIZE 6
#define SEQSPACE 7
#define NOTINUSE (-1)
#define RCV_WINDOWSIZE 6

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
static struct pkt recv_buffer[SEQSPACE];
static bool packet_received[SEQSPACE];
static bool acked[SEQSPACE];

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
    acked[sendpkt.seqnum] = false;

    windowlast = (windowfirst + windowcount) % WINDOWSIZE;
    buffer[windowlast] = sendpkt;
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
  int ackcount = 0;
  int i;
  int index = windowfirst;
  int seqfirst, seqlast;

  acked[packet.acknum] = true;

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      seqfirst = buffer[windowfirst].seqnum;
      seqlast = buffer[windowlast].seqnum;

      if (((seqfirst <= seqlast) && (packet.acknum >= seqfirst && packet.acknum <= seqlast)) ||
          ((seqfirst > seqlast) && (packet.acknum >= seqfirst || packet.acknum <= seqlast))) {

        if (TRACE > 0)
          printf("----A: ACK %d is not a duplicate\n", packet.acknum);
        new_ACKs++;

        for (i = 0; i < windowcount; i++) {
          if (buffer[index].seqnum == packet.acknum) {
            ackcount = i + 1;
            break;
          }
          index = (index + 1) % WINDOWSIZE;
        }

        windowfirst = (windowfirst + ackcount) % WINDOWSIZE;

        for (i = 0; i < ackcount; i++)
          windowcount--;

        stoptimer(A);
        if (windowcount > 0)
          starttimer(A, RTT);
      }
    } else {
      if (TRACE > 0)
        printf("----A: duplicate ACK received, do nothing!\n");
    }
  } else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}

void A_timerinterrupt(void)
{
  int i;
  int index, seq;

  if (TRACE > 0)
    printf("----A: time out, resend packets!\n");

  for (i = 0; i < windowcount; i++) {
    index = (windowfirst + i) % WINDOWSIZE;
    seq = buffer[index].seqnum;
    if (!acked[seq]) {
      if (TRACE > 0)
        printf("---A: resending packet %d\n", seq);
      tolayer3(A, buffer[index]);
      packets_resent++;
      break;
    }
  }

  if (windowcount > 0)
    starttimer(A, RTT);
}

void A_init(void)
{
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;

  for (i = 0; i < SEQSPACE; i++) {
    acked[i] = false;
  }

  if (TRACE > 0)
    printf("----A: Initialization complete\n");
}

/********* Receiver (B) variables and procedures ************/

static int expectedseqnum;
static int B_nextseqnum;

void B_input(struct pkt packet)
{
  struct pkt ackpkt;
  int i;
  int seq = packet.seqnum;
  int last_acked;

  if (!IsCorrupted(packet)) {
    if (!packet_received[seq]) {
      recv_buffer[seq] = packet;
      packet_received[seq] = true;
      if (TRACE > 0)
        printf("----B: packet %d buffered\n", seq);
    } else {
      if (TRACE > 0)
        printf("----B: duplicate packet %d received\n", seq);
    }

    ackpkt.acknum = seq;
    ackpkt.seqnum = B_nextseqnum++;
    for (i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);

    while (packet_received[expectedseqnum]) {
      tolayer5(B, recv_buffer[expectedseqnum].payload);
      packet_received[expectedseqnum] = false;
      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
    }

  } else {
    if (TRACE > 0)
      printf("----B: corrupted packet received, sending ACK for last in-order packet\n");

    last_acked = (expectedseqnum + SEQSPACE - 1) % SEQSPACE;
    ackpkt.acknum = last_acked;
    ackpkt.seqnum = B_nextseqnum++;
    for (i = 0; i < 20; i++) ackpkt.payload[i] = '0';
    ackpkt.checksum = ComputeChecksum(ackpkt);
    tolayer3(B, ackpkt);
  }
}

void B_init(void)
{
  int i;
  expectedseqnum = 0;
  B_nextseqnum = 1;
  for (i = 0; i < SEQSPACE; i++) {
    packet_received[i] = false;
  }
}

void B_output(struct msg message)
{
}

void B_timerinterrupt(void)
{
}
