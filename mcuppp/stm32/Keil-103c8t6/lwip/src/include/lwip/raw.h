#ifndef LWIP_HDR_RAW_H
#define LWIP_HDR_RAW_H

#include "lwip/opt.h"
#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"

struct raw_pcb;

struct raw_pcb *raw_new(int proto);
void raw_recv(struct raw_pcb *pcb, void (*recv_fn)(void *, struct raw_pcb *, struct pbuf *, const ip_addr_t *), void *recv_arg);
void raw_bind(struct raw_pcb *pcb, const ip_addr_t *ipaddr);
void raw_remove(struct raw_pcb *pcb);
err_t raw_sendto(struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr);

#endif /* LWIP_HDR_RAW_H */
