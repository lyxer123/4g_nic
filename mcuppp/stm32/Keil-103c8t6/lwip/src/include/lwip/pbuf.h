#ifndef LWIP_HDR_PBUF_H
#define LWIP_HDR_PBUF_H

#include <stdint.h>
#include <stddef.h>

struct pbuf {
  struct pbuf *next;
  void *payload;
  uint16_t len;
  uint16_t tot_len;
};

#define PBUF_IP 0
#define PBUF_POOL 0

struct pbuf *pbuf_alloc(int layer, uint16_t length, int type);
void pbuf_free(struct pbuf *p);

#endif /* LWIP_HDR_PBUF_H */
