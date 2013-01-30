/* apurvb
   23 Jan 2013

   randomized TDMA scheduler to drive the USRPs in a multihop setting.
   The scheduler has the following behavior: 
   1. Any node that wants the medium sends a REQUEST_INIT message. 
   2. The scheduler works in rounds and collects all the pending REQUEST_INIT 
      messages in that round. It then groups them into a timeslot and enqueues in the
      timeslot vector.
   3. It checks the front of the timeslot vector and services the clients in the first
      time slot. A request in the current timeslot is picked randomly and scheduled. To
      confirm scheduling, the scheduler replies the node with a REPLY_MSG.
   4. The replied-to client node then does its job and sends the scheduler a REQUEST_COMPLETE
      message to signal the end of this service request. The scheduler then continues in its
      usual merry way. 
*/

#include <usrp_scheduler.h>
using namespace std;

int d_port = 9000;
int main (int argc, const char* argv[]) {
  if (argc != 2) {
    fprintf (stderr, " usage: %s <num_clients> \n", argv[0]);
    exit(1);
  }

   int num_clients = atoi(argv[1]);
   printf("size: %d\n", sizeof(SchedulerMsg)); 
   populateSocketInfo();
   open_server_sock(d_port, d_client_socks, num_clients);
   while(1) {
      // check the socket to see if any pending requests //
      checkSocketForPendingRequests();

      SchedulerMsg msg;
      bool have_req = getSchedulingRequest(msg);
      if(have_req) {
         assert(msg.type == REQUEST_INIT_MSG);
	 msg.type = REPLY_MSG;
	 giveSchedulingReply(msg);
	 waitForCompleteReply(msg);			// blocking!
	 sleep(0.7);
      }
   }

}

// non-blocking //
void checkSocketForPendingRequests() {
   //printf("checkSocketForPendingRequests\n"); fflush(stdout);
   int buf_size = sizeof(SchedulerMsg);
   char *req_buf = (char*) malloc(buf_size);
   memset(req_buf, 0, buf_size);

   RequestVector req_vec;

   int n_extracted = 0;
   for(int i = 0; i < d_client_socks.size(); i++) { 
       int nbytes = recv(d_client_socks[i], req_buf, buf_size, MSG_PEEK);
       if(nbytes > 0) {
	  int offset = 0;
	  printf("nbytes: %d\n", nbytes); fflush(stdout);
	  while(1) {
	     nbytes = recv(d_client_socks[i], req_buf+offset, buf_size-offset, 0);
	     if(nbytes > 0) offset += nbytes;
	     //printf("nbytes: %d, offset: %d, buf_size: %d\n", nbytes, offset, buf_size); 
	     if(offset == buf_size) {
		SchedulerMsg* req = (SchedulerMsg*) malloc(buf_size);
	        memcpy(req, req_buf, buf_size);
		assert(req->type == REQUEST_INIT_MSG);
		req_vec.push_back(req);
		d_sockMap.insert(std::pair<NodeId, SockId> (req->nodeId, d_client_socks[i]));
		n_extracted++;
		printf("-- received request, nodeId: %c, request_id: %d\n", req->nodeId, req->request_id); fflush(stdout);
		break;					// move to next client
	     }
	  } // while
       }
   } // for

   if(req_vec.size() > 0) {
      createTimeSlot(req_vec);
   }
   free(req_buf);
}

/* check the currentTimeSlot for any remaining requests, 
   if yes, then randomly select one */
bool getSchedulingRequest(SchedulerMsg& request) {
   //printf("getSchedulingRequest\n"); fflush(stdout);
   if(d_timeSlotVector.size() == 0) 
	return false;

   TimeSlot *currTimeSlot = d_timeSlotVector.front();
   assert(currTimeSlot != NULL);

   int num_req = currTimeSlot->num_requests;
   assert(num_req > 0);
   int id = selectRequestToFulfil(num_req);

   RequestVector* pendingReq = currTimeSlot->requests;
   SchedulerMsg *req = pendingReq->at(id);
   request.nodeId = req->nodeId;
   request.request_id = req->request_id;
   request.type = REQUEST_INIT_MSG;

   free(req);
   pendingReq->erase(pendingReq->begin()+id);
   num_req--;
   
   if(num_req == 0) {
      free(pendingReq);
      d_timeSlotVector.erase(d_timeSlotVector.begin());
      free(currTimeSlot);	
   }
   
   currTimeSlot->num_requests = num_req;
   printf("-- selected request, nodeId: %c, request_id: %d\n", request.nodeId, request.request_id); fflush(stdout);
   return true;
}

void giveSchedulingReply(SchedulerMsg msg) {
   printf("giveSchedulingReply, nodeId: %c, request_id: %d\n", msg.nodeId, msg.request_id); fflush(stdout);
   int buf_size = sizeof(SchedulerMsg);
   char *reply_buf = (char*) malloc(buf_size);
   memcpy(reply_buf, &msg, buf_size);

#if 1
   // give the reply only to the corresponding socket //
   SockMap::iterator it = d_sockMap.find(msg.nodeId);
   assert(it != d_sockMap.end());
  
   SockId sockfd = it->second;
   if(send(sockfd, (char*) &reply_buf[0], sizeof(SchedulerMsg), 0) < 0) 
      assert(false);
#endif
   free(reply_buf);
}

// blocking! //
void waitForCompleteReply(SchedulerMsg msg) {
   printf("waitForCompleteReply\n"); fflush(stdout);
   int buf_size = sizeof(SchedulerMsg);
   char *buf = (char*) malloc(buf_size);
   memset(buf, 0, buf_size);

   SockMap::iterator it = d_sockMap.find(msg.nodeId);
   assert(it != d_sockMap.end());
   SockId sockfd = it->second;

   bool done = false;
   while(!done) {
      int nbytes = recv(sockfd, buf, buf_size, MSG_PEEK);
      if(nbytes > 0) {
         int offset = 0;
         while(1) {
	    nbytes = recv(sockfd, buf+offset, buf_size-offset, 0);					// blocking!
	    if(nbytes > 0) offset += nbytes;
	    if(offset == buf_size) {
	       SchedulerMsg reply;
	       memcpy(&reply, buf, buf_size);
	       assert(reply.type == REQUEST_COMPLETE_MSG && reply.nodeId == msg.nodeId && reply.request_id == msg.request_id);
	       printf("-- received reply, nodeId: %c, request_id: %d\n", reply.nodeId, reply.request_id); fflush(stdout);
	       done = true;
	       break;
            }
         } // while
      } // (nbytes > 0)
   } // while(1)
   free(buf);
}

/* creates a new time slot based on the requests received */
void createTimeSlot(RequestVector requests) {
   printf("createTimeSlot\n"); fflush(stdout);
   TimeSlot *slot = (TimeSlot*) malloc(sizeof(TimeSlot));
   memset(slot, 0, sizeof(TimeSlot));
   slot->num_requests = requests.size();
   
   RequestVector *reqVec = (RequestVector*) malloc(sizeof(RequestVector));
   memset(reqVec, 0, sizeof(RequestVector));
   for(int i = 0; i < requests.size(); i++) {
      reqVec->push_back(requests[i]);
   }
 
   slot->requests = reqVec;
   d_timeSlotVector.push_back(slot);
}

int selectRequestToFulfil(int num_requests) {
   printf("selectRequestToFulfil, num_requests: %d\n", num_requests);
   return rand() % num_requests;
}

/***************************************  some utility functions *******************************/
void open_server_sock(int sock_port, vector<unsigned int>& connected_clients, int num_clients) {
  printf("open_server_sock start, #clients: %d\n", num_clients); fflush(stdout);
  int sockfd, _sock;
  struct sockaddr_in dest;

  /* create socket */
  sockfd = socket(PF_INET, SOCK_STREAM, 0);
  //fcntl(sockfd, F_SETFL, O_NONBLOCK);

  /* ensure the socket address can be reused, even after CTRL-C the application */
  int optval = 1;
  int ret = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));

  if(ret == -1) {
        printf("@ digital_ofdm_mapper_bcv::setsockopt ERROR\n"); fflush(stdout);
  }

  /* initialize structure dest */
  bzero(&dest, sizeof(dest));
  dest.sin_family = AF_INET;

  /* assign a port number to socket */
  dest.sin_port = htons(sock_port);
  dest.sin_addr.s_addr = INADDR_ANY;

  bind(sockfd, (struct sockaddr*)&dest, sizeof(dest));

  /* make it listen to socket with max 20 connections */
  listen(sockfd, 1);
  assert(sockfd != -1);

  struct sockaddr_in client_addr;
  int addrlen = sizeof(client_addr);

  int conn = 0;
  while(conn != num_clients) {
    _sock = accept(sockfd, (struct sockaddr*)&client_addr, (socklen_t*)&addrlen);
    fcntl(_sock, F_SETFL, O_NONBLOCK);
    if(_sock != -1) {
      printf(" -- connected client: %d\n", conn); fflush(stdout);
      connected_clients.push_back(_sock);
      conn++;
    }
  }

  printf("open_server_sock done.. #clients: %d\n", num_clients);
  assert(connected_clients.size() == num_clients);
}

void populateSocketInfo() {
   // node-id ethernet-addrees port-on-which-it-is-listening //
   printf("populateSocketInfo\n"); fflush(stdout);
   FILE *fl = fopen ( "eth_scheduler.txt" , "r+" );
   if (fl==NULL) {
        fprintf (stderr, "File error\n");
        exit (1);
   }

   char line[128];
   const char *delim = " ";
   while ( fgets ( line, sizeof(line), fl ) != NULL ) {
        char *strtok_res = strtok(line, delim);
        vector<char*> token_vec;
        while (strtok_res != NULL) {
           token_vec.push_back(strtok_res);
           //printf ("%s\n", strtok_res);
           strtok_res = strtok (NULL, delim);
        }

        EthInfo *ethInfo = (EthInfo*) malloc(sizeof(EthInfo));
        memset(ethInfo, 0, sizeof(EthInfo));
        NodeId node_id = (NodeId) atoi(token_vec[0]) + '0';
        memcpy(ethInfo->addr, token_vec[1], 16);
        unsigned int port = atoi(token_vec[2]);
        ethInfo->port = port;

        d_ethInfoMap.insert(pair<NodeId, EthInfo*> (node_id, ethInfo));

        //std::cout<<"Inserting in the map: node " << node_id << " addr: " << ethInfo->addr << endl;
   }

    fclose (fl);
}


