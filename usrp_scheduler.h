#include <stdio.h>
#include <stdlib.h>
#include <cstdio>
#include <time.h>
#include <string.h>
#include <assert.h>
#include <vector>
#include <bitset>
#include <map>


#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdexcept>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <math.h>

#define REQUEST_INIT_MSG 1
#define REPLY_MSG 2
#define REQUEST_COMPLETE_MSG 3

using namespace std;

typedef unsigned char NodeId;
typedef unsigned int SockId;
typedef map<NodeId, SockId> SockMap;

typedef struct eth_info {
  char addr[16];
  int port;
} EthInfo;

typedef map<NodeId, EthInfo*> EthInfoMap;

/* everytime the scheduler checks a socket, all pending requests result in scheduler requests */
#pragma pack(1)
typedef struct scheduler_msg {
  int request_id;
  NodeId nodeId;
  int type;
} SchedulerMsg;
typedef vector<SchedulerMsg*> RequestVector;

/* every time slot will have a requestVector */
typedef struct time_slot {
  int num_requests; 
  RequestVector* requests;
} TimeSlot;
typedef vector<TimeSlot*> TimeSlotVector;

/* socket functions */
void open_server_sock(int, vector<unsigned int>&, int);
void populateSocketInfo();
vector<SockId> d_client_socks;
SockMap d_sockMap;
EthInfoMap d_ethInfoMap;

/* time slot management */
void createTimeSlot(RequestVector requests);
TimeSlotVector d_timeSlotVector;

/* request management */
void checkSocketForPendingRequests();
bool getSchedulingRequest(SchedulerMsg& request);
void giveSchedulingReply(SchedulerMsg msg);
void waitForCompleteReply(SchedulerMsg msg);
int selectRequestToFulfil(int num_requests);



