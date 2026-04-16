/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : pppos_lwip.h
  * @brief          : PPPoS + lwIP helper interface for STM32.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __PPPOS_LWIP_H
#define __PPPOS_LWIP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f1xx_hal.h"

/* NOTE: You must add lwIP sources to the project and enable PPP support. */
#include "lwip/err.h"
#include "lwip/netif.h"
#include "lwip/pppapi.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"

void PPPOS_Init(UART_HandleTypeDef *huart);
void PPPOS_Process(void);
uint8_t PPPOS_IsLinkUp(void);

#ifdef __cplusplus
}
#endif

#endif /* __PPPOS_LWIP_H */
