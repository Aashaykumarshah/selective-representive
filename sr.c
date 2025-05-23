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
static float timer_expiry[WINDOWSIZE];  
static bool timer_active[WINDOWSIZE];   
static float current_time = 0.0;      
static int last_acked_seq = -1; 

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
    timer_expiry[windowlast] = current_time + RTT;
    timer_active[windowlast] = true;


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
  int i, index;
  bool has_unacked = false;
  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----A: uncorrupted ACK %d is received\n", packet.acknum);
    total_ACKs_received++;

    if (windowcount == 0)
      return;

    index = windowfirst;
    for (i = 0; i < windowcount; i++) {
      if ((buffer[index].seqnum == packet.acknum) && (!acked[index])) {
        if (!acked[index]) {
          if (TRACE > 0)
            printf("----A: ACK %d is not a duplicate\n", packet.acknum);
          new_ACKs++;
          acked[index] = true;
          timer_active[index] = false;
        } else {
          if (TRACE > 0)
            printf("----A: duplicate ACK received, do nothing!\n");
        }
        break;
      }
      index = (index + 1) % WINDOWSIZE;
    }

    /* Slide window forward only over in-order ACKed packets */
    while (windowcount > 0 && acked[windowfirst]) {
      acked[windowfirst] = false;
      windowfirst = (windowfirst + 1) % WINDOWSIZE;
      windowcount--;
    }
 
    stoptimer(A);
    
    for (i = 0; i < windowcount; i++) {
      if (!acked[(windowfirst + i) % WINDOWSIZE]) {
        has_unacked = true;
        break;
       }
      }
      if (has_unacked)
        starttimer(A, RTT);
  } else {
    if (TRACE > 0)
      printf("----A: corrupted ACK is received, do nothing!\n");
  }
}


void A_timerinterrupt(void)
{
  int i;
 

  current_time += RTT;  
  if (TRACE > 0) 
    printf("----A: time out,resend packets!\n");
  for (i = 0; i < windowcount; i++) {
    int index = (windowfirst + i) % WINDOWSIZE;
    if (!acked[index] && timer_active[index] && current_time >= timer_expiry[index]) {
      if (TRACE > 0)
       printf("---A: resending packet %d\n", buffer[index].seqnum);
      
      tolayer3(A, buffer[index]);
      packets_resent++;
      timer_expiry[index] = current_time + RTT;
      break; 
   }
 }
 for (i = 0; i < windowcount; i++) {
    int index = (windowfirst + i) % WINDOWSIZE;
    if (!acked[index]) {
      starttimer(A, RTT);
      return;
    }
  }
}




void A_init(void)
{
  int i;
  A_nextseqnum = 0;
  windowfirst = 0;
  windowlast = -1;
  windowcount = 0;
  for (i = 0; i < WINDOWSIZE; i++) {
    timer_active[i] = false;
    timer_expiry[i] = 0.0;
  }
  current_time = 0.0;
}

/********* Receiver (B)  variables and procedures ************/

static int expectedseqnum;
static int B_nextseqnum;

#define RCV_BUFFER_SIZE SEQSPACE

static struct pkt recv_buffer[RCV_BUFFER_SIZE];
static bool received[RCV_BUFFER_SIZE];
void B_input(struct pkt packet)
{
  struct pkt ackpkt;
  int i;

  if (!IsCorrupted(packet)) {
    if (TRACE > 0)
      printf("----B: packet %d is correctly received, send ACK!\n", packet.seqnum);
   packets_received++;

    /* Store packet in buffer if within window */
    if (!received[packet.seqnum]) {
      recv_buffer[packet.seqnum] = packet;
      received[packet.seqnum] = true;
    }

    /* Deliver all in-order packets starting from expectedseqnum */
    while (received[expectedseqnum]) {
      tolayer5(B, recv_buffer[expectedseqnum].payload);
      received[expectedseqnum] = false;
      expectedseqnum = (expectedseqnum + 1) % SEQSPACE;
    }

    /* Send ACK for this packet */
    last_acked_seq = packet.seqnum;
    ackpkt.acknum = packet.seqnum;
  } else {
    if (TRACE > 0)
      printf("----B: packet corrupted or not expected sequence number, resend ACK!\n");

    ackpkt.acknum = last_acked_seq;

  }

  ackpkt.seqnum = B_nextseqnum;
  B_nextseqnum = (B_nextseqnum + 1) % 2;

  for (i = 0; i < 20; i++)
    ackpkt.payload[i] = '0';

  ackpkt.checksum = ComputeChecksum(ackpkt);
  tolayer3(B, ackpkt);
}

void B_init(void)
{
  int i;
expectedseqnum = 0;
B_nextseqnum = 1;
last_acked_seq = SEQSPACE - 1;

for (i = 0; i < RCV_BUFFER_SIZE; i++) {
  received[i] = false;
}
}

void B_output(struct msg message)
{
}

void B_timerinterrupt(void)
{
}
