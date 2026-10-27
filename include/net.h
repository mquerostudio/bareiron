#ifndef H_NET
#define H_NET

#include <stddef.h>
#include <unistd.h>

// Sits between the packet handlers and the sockets. Writes go to a
// per-client buffer and leave in whole frames without ever blocking the
// server loop. A client whose backlog grows past NET_TX_CAP is dropped.

#define NET_TX_CAP (4u * 1024 * 1024)

int     net_open (int fd);                 // 0 ok, -1 no free slot
void    net_close (int fd);                // frees buffers; closes nothing
ssize_t net_write (int fd, const void *buf, size_t len); // appends; returns len, or -1 if fd unknown
int     net_flush (int fd);                // frame + non-blocking send; 0 ok, -1 peer gone, -2 over NET_TX_CAP
size_t  net_pending (int fd);              // bytes waiting in the wire buffer

#endif
