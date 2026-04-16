/*
 * Minimal placeholder for lwIP PPP sources.
 * Replace this with actual lwIP PPP source files.
 */

#include "lwip/pppapi.h"
#include <stdlib.h>

struct ppp_pcb {
  void *ctx;
};

struct ppp_pcb *pppapi_pppos_create(struct netif *pppif, u32_t (*output)(void *, u8_t *, u32_t), ppp_status_cb_fn status_cb, void *ctx)
{
  (void)pppif;
  (void)output;
  (void)status_cb;
  (void)ctx;
  struct ppp_pcb *pcb = (struct ppp_pcb *)malloc(sizeof(struct ppp_pcb));
  if (pcb != NULL)
  {
    pcb->ctx = ctx;
  }
  return pcb;
}

void pppapi_set_default(struct ppp_pcb *pcb)
{
  (void)pcb;
}

void ppp_set_usepeerdns(struct ppp_pcb *pcb, int usepeerdns)
{
  (void)pcb;
  (void)usepeerdns;
}

void pppapi_connect(struct ppp_pcb *pcb, int mode)
{
  (void)pcb;
  (void)mode;
}

void pppos_input_byte(struct ppp_pcb *pcb, u8_t byte)
{
  (void)pcb;
  (void)byte;
}
