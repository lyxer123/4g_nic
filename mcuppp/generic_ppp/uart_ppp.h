#ifndef UART_PPP_H
#define UART_PPP_H

#include <stdint.h>
#include "lwip/pppapi.h"

#ifdef __cplusplus
extern "C" {
#endif

void uart_ppp_init(void);
void uart_ppp_poll(void);
uint32_t uart_ppp_output(ppp_pcb *pcb, uint8_t *data, uint32_t len);

#ifdef __cplusplus
}
#endif

#endif // UART_PPP_H
