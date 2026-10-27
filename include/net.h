#ifndef H_NET
#define H_NET

#include <stddef.h>
#include <unistd.h>

// Sits between the packet handlers and the sockets. Writes go to a
// per-client buffer and leave in whole frames without ever blocking the
// server loop. A client whose backlog grows past NET_TX_CAP is dropped.
// Reads only start once a whole packet has arrived, so a handler never
// waits on the network.

#define NET_TX_CAP (4u * 1024 * 1024)
#define NET_MAX_PACKET (2u * 1024 * 1024)

int     net_open (int fd);                 // 0 ok, -1 no free slot
void    net_close (int fd);                // frees buffers; closes nothing
ssize_t net_write (int fd, const void *buf, size_t len); // appends; returns len, or -1 if fd unknown
int     net_flush (int fd);                // frame + non-blocking send; 0 ok, -1 peer gone, -2 over NET_TX_CAP
size_t  net_pending (int fd);              // bytes waiting in the wire buffer

int     net_pump (int fd);                         // read what's available; 0 ok, -1 peer gone/error
int     net_next_packet (int fd, int *length);     // 1 = a whole packet is current, 0 = not yet, -1 = bad frame
ssize_t net_read (int fd, void *buf, size_t n);    // reads from the current packet; returns < n at its end
void    net_finish_packet (int fd);                // drops unread bytes of the current packet

// Switches a client to the compressed frame format. Frames written before
// this call still leave uncompressed.
void    net_enable_compression (int fd, int threshold);

#endif
