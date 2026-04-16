#ifndef LWIP_HDR_NETDB_H
#define LWIP_HDR_NETDB_H

#include <stdint.h>

struct hostent {
  char *h_name;
  char **h_aliases;
  int h_addrtype;
  int h_length;
  char **h_addr_list;
};

struct hostent *gethostbyname(const char *name);

#endif /* LWIP_HDR_NETDB_H */
