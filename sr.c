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

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount != 0) {
      int seqfirst = buffer[windowfirst].seqnum;
      int seqlast = buffer[windowlast].seqnum;

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
        
        
        if (ackcount == 0) {
          if (TRACE > 0)
            printf("----A: ACK %d not found in window, possibly already processed\n", packet.acknum);
          return;
        }

        windowfirst = (windowfirst + ackcount) % WINDOWSIZE;

        for (i = 0; i < ackcount; i++)
          windowcount--;

        stoptimer(A);
        if (windowcount > 0)
          starttimer(A, RTT);
      } else {
        if (TRACE > 0)
          printf("----A: duplicate ACK received, do nothing!\n");
      }
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
    if (TRACE > 1)
      printf("----A: Resending packet %d\n", buffer[index].seqnum);

    tolayer3(A, buffer[index]);
    index = (index + 1) % WINDOWSIZE;
  }

  starttimer(A, RTT);

  if (TRACE > 1)
    printf("----A: Timer restarted after retransmission\n");
}

void A_init(void)
{
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
}

/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum;
static int B_nextseqnum;

void B_input(struct pkt packet)
{
  struct pkt sendpkt;
  int i;

  if ((!IsCorrupted(packet)) && (packet.seqnum == expectedseqnum)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
    packets_received++;

    tolayer5(B, packet.payload);

    sendpkt.acknum = expectedseqnum;
    expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
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
  expectedseqnum = 0;
  B_nextseqnum = 1;
}

void B_output(struct msg message)
{
}

void B_timerinterrupt(void)
{
}
