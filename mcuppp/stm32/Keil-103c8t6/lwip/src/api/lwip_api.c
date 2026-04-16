/*
 * Minimal placeholder for lwIP API sources.
 * Replace this with actual lwIP API source files.
 */

#include "lwip/opt.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include <stdlib.h>
#include <string.h>

struct hostent *gethostbyname(const char *name)
{
  (void)name;
  return NULL;
}

int socket(int domain, int type, int protocol)
{
  (void)domain;
  (void)type;
  (void)protocol;
  return -1;
}

int connect(int s, const struct sockaddr *name, socklen_t namelen)
{
  (void)s;
  (void)name;
  (void)namelen;
  return -1;
}

int send(int s, const void *data, size_t size, int flags)
{
  (void)s;
  (void)data;
  (void)size;
  (void)flags;
  return -1;
}

int recv(int s, void *mem, size_t len, int flags)
{
  (void)s;
  (void)mem;
  (void)len;
  (void)flags;
  return -1;
}

int closesocket(int s)
{
  (void)s;
  return 0;
}
