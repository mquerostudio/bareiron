#ifndef H_DISPATCH
#define H_DISPATCH

// Routes an incoming packet to its packet handler or procedure
void handlePacket (int client_fd, int length, int packet_id, int state);

// Asks the server loop to disconnect a client once the current packet is
// handled (server.c)
void kickClient (int client_fd);

#endif
