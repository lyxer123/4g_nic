#ifndef LWIP_HDR_PPPAPI_H
#define LWIP_HDR_PPPAPI_H

#include "lwip/netif.h"
#include "lwip/err.h"
#include "lwip/ppp.h"

struct ppp_pcb;
struct netif;

typedef void (*ppp_status_cb_fn)(void *ctx, int err_code, struct ppp_pcb *pcb);

struct ppp_pcb *pppapi_pppos_create(struct netif *pppif, u32_t (*output)(void *, u8_t *, u32_t), ppp_status_cb_fn status_cb, void *ctx);
void pppapi_set_default(struct ppp_pcb *pcb);
void ppp_set_usepeerdns(struct ppp_pcb *pcb, int usepeerdns);
void pppapi_connect(struct ppp_pcb *pcb, int mode);
void pppos_input_byte(struct ppp_pcb *pcb, u8_t byte);

#endif /* LWIP_HDR_PPPAPI_H */
