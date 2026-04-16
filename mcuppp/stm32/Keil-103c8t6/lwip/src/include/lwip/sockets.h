#ifndef LWIP_HDR_SOCKETS_H
#define LWIP_HDR_SOCKETS_H

#include <stddef.h>
#include <stdint.h>

typedef size_t socklen_t;

#define AF_INET 2
#define SOCK_STREAM 1
#define IPPROTO_TCP 6

struct sockaddr {
  uint8_t sa_len;
  uint8_t sa_family;
  char sa_data[14];
};

struct sockaddr_in {
  uint8_t sin_len;
  uint8_t sin_family;
  uint16_t sin_port;
  uint32_t sin_addr;
  char sin_zero[8];
};

int socket(int domain, int type, int protocol);
int connect(int s, const struct sockaddr *name, socklen_t namelen);
int send(int s, const void *data, size_t size, int flags);
int recv(int s, void *mem, size_t len, int flags);
int closesocket(int s);

#endif /* LWIP_HDR_SOCKETS_H */
