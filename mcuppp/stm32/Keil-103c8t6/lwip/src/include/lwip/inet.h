#ifndef LWIP_HDR_INET_H
#define LWIP_HDR_INET_H

#include "lwip/ip_addr.h"

const char *ipaddr_ntoa(const ip_addr_t *addr);
int ipaddr_aton(const char *cp, ip_addr_t *addr);

#endif /* LWIP_HDR_INET_H */
