/*
 * Minimal placeholder for lwIP netif sources.
 * Replace this with actual lwIP netif source files.
 */

#include "lwip/netif.h"
#include "lwip/raw.h"
#include "lwip/pbuf.h"
#include "lwip/err.h"
#include <stdlib.h>
#include <string.h>

struct raw_pcb {
  int proto;
  void (*recv_fn)(void *, struct raw_pcb *, struct pbuf *, const ip_addr_t *);
  void *recv_arg;
  const ip_addr_t *bound_ip;
};

struct raw_pcb *raw_new(int proto)
{
  struct raw_pcb *pcb = (struct raw_pcb *)malloc(sizeof(struct raw_pcb));
  if (pcb != NULL)
  {
    pcb->proto = proto;
    pcb->recv_fn = NULL;
    pcb->recv_arg = NULL;
    pcb->bound_ip = NULL;
  }
  return pcb;
}

void raw_recv(struct raw_pcb *pcb, void (*recv_fn)(void *, struct raw_pcb *, struct pbuf *, const ip_addr_t *), void *recv_arg)
{
  if (pcb != NULL)
  {
    pcb->recv_fn = recv_fn;
    pcb->recv_arg = recv_arg;
  }
}

void raw_bind(struct raw_pcb *pcb, const ip_addr_t *ipaddr)
{
  if (pcb != NULL)
  {
    pcb->bound_ip = ipaddr;
  }
}

void raw_remove(struct raw_pcb *pcb)
{
  free(pcb);
}

err_t raw_sendto(struct raw_pcb *pcb, struct pbuf *p, const ip_addr_t *addr)
{
  (void)pcb;
  (void)p;
  (void)addr;
  return ERR_OK;
}

struct pbuf *pbuf_alloc(int layer, uint16_t length, int type)
{
  (void)layer;
  (void)type;
  struct pbuf *p = (struct pbuf *)malloc(sizeof(struct pbuf));
  if (p != NULL)
  {
    p->payload = malloc(length);
    if (p->payload == NULL)
    {
      free(p);
      return NULL;
    }
    p->len = (uint16_t)length;
    p->tot_len = (uint16_t)length;
    memset(p->payload, 0, length);
    p->next = NULL;
  }
  return p;
}

void pbuf_free(struct pbuf *p)
{
  if (p != NULL)
  {
    free(p->payload);
    free(p);
  }
}
