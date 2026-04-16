/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : lwip_app.h
  * @brief          : Application helpers for lwIP tests.
  ******************************************************************************
  */
/* USER CODE END Header */

#ifndef __LWIP_APP_H
#define __LWIP_APP_H

#ifdef __cplusplus
extern "C" {
#endif

#include "lwip/opt.h"

void LWIP_PingTest(const char *ipv4_addr);
void LWIP_HttpGetTest(const char *host, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* __LWIP_APP_H */
