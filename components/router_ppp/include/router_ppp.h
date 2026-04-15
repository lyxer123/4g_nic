#ifndef ROUTER_PPP_H
#define ROUTER_PPP_H

#include <stdbool.h>
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

void router_ppp_start(void);
bool router_ppp_is_running(void);
esp_netif_t *router_ppp_get_netif(void);

#ifdef __cplusplus
}
#endif

#endif // ROUTER_PPP_H
