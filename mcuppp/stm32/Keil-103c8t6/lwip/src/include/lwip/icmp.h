#ifndef LWIP_HDR_ICMP_H
#define LWIP_HDR_ICMP_H

#include "lwip/inet.h"
#include "lwip/pbuf.h"

#define ICMP_ECHO 8

struct icmp_echo_hdr {
  uint8_t type;
  uint8_t code;
  uint16_t chksum;
  uint16_t id;
  uint16_t seqno;
};

#define ICMPH_TYPE_SET(i, t) ((i)->type = (t))
#define ICMPH_CODE_SET(i, c) ((i)->code = (c))

uint16_t inet_chksum(const void *dataptr, size_t len);

#endif /* LWIP_HDR_ICMP_H */
