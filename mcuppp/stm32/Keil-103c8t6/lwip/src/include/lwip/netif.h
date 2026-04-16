#ifndef LWIP_HDR_NETIF_H
#define LWIP_HDR_NETIF_H

#include "lwip/ip_addr.h"

struct netif {
  ip_addr_t ip_addr;
  ip_addr_t netmask;
  ip_addr_t gw;
};

#endif /* LWIP_HDR_NETIF_H */
