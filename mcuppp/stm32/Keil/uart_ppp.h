#ifndef UART_PPP_H
#define UART_PPP_H

#include "stm32f4xx_hal.h"
#include <stdint.h>

HAL_StatusTypeDef uart_ppp_init(void);
HAL_StatusTypeDef uart_ppp_send(const uint8_t *data, uint32_t len);
void uart_ppp_poll(void);
void uart_ppp_receive_callback(uint8_t *data, uint16_t len);

#endif // UART_PPP_H
