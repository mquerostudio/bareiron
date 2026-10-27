#ifndef H_DISPATCH
#define H_DISPATCH

// Routes an incoming packet to its packet handler or procedure
void handlePacket (int client_fd, int length, int packet_id, int state);

#endif
