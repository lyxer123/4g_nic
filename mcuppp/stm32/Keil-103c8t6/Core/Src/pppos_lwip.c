/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : pppos_lwip.c
  * @brief          : PPPoS over UART helper based on lwIP.
  ******************************************************************************
  */
/* USER CODE END Header */

#include "pppos_lwip.h"
#include "main.h"
#include <stdio.h>
#include <string.h>

static UART_HandleTypeDef *pppos_huart = NULL;
static ppp_pcb *ppp = NULL;
static struct netif ppp_netif;
static uint8_t uart_rx_byte;
static volatile uint8_t ppp_link_up = 0;

static void PPPOS_DebugPrint(const char *msg)
{
  if (pppos_huart != NULL)
  {
    HAL_UART_Transmit(pppos_huart, (uint8_t *)msg, strlen(msg), HAL_MAX_DELAY);
  }
}

static u32_t pppos_output_cb(void *ctx, u8_t *data, u32_t len)
{
  if (pppos_huart == NULL || len == 0)
  {
    return 0;
  }
  if (HAL_UART_Transmit(pppos_huart, data, len, HAL_MAX_DELAY) != HAL_OK)
  {
    return 0;
  }
  return len;
}

static void ppp_status_cb(void *ctx, int err_code, struct ppp_pcb *pcb)
{
  char buf[128];

  switch (err_code)
  {
    case PPPERR_NONE:
      ppp_link_up = 1;
      snprintf(buf, sizeof(buf), "PPPOS: LINK UP\r\n");
      PPPOS_DebugPrint(buf);
      break;
    case PPPERR_PARAM:
      PPPOS_DebugPrint("PPPOS: PARAM ERROR\r\n");
      break;
    case PPPERR_OPEN:
      PPPOS_DebugPrint("PPPOS: OPEN ERROR\r\n");
      break;
    case PPPERR_DEVICE:
      PPPOS_DebugPrint("PPPOS: DEVICE ERROR\r\n");
      break;
    case PPPERR_ALLOC:
      PPPOS_DebugPrint("PPPOS: ALLOC ERROR\r\n");
      break;
    case PPPERR_USER:
      PPPOS_DebugPrint("PPPOS: USER DISCONNECT\r\n");
      break;
    case PPPERR_CONNECT:
      PPPOS_DebugPrint("PPPOS: CONNECT ERROR\r\n");
      break;
    case PPPERR_ABORT:
      PPPOS_DebugPrint("PPPOS: ABORT\r\n");
      break;
    case PPPERR_PROTOCOL:
      PPPOS_DebugPrint("PPPOS: PROTOCOL ERROR\r\n");
      break;
    case PPPERR_PEERDEAD:
      PPPOS_DebugPrint("PPPOS: PEER DEAD\r\n");
      break;
    case PPPERR_IDLETIMEOUT:
      PPPOS_DebugPrint("PPPOS: IDLE TIMEOUT\r\n");
      break;
    case PPPERR_CONNECTTIMEOUT:
      PPPOS_DebugPrint("PPPOS: CONNECT TIMEOUT\r\n");
      break;
    case PPPERR_LOOPBACK:
      PPPOS_DebugPrint("PPPOS: LOOPBACK DETECTED\r\n");
      break;
    default:
      snprintf(buf, sizeof(buf), "PPPOS: STATUS %d\r\n", err_code);
      PPPOS_DebugPrint(buf);
      break;
  }

  if (ppp_link_up && ppp != NULL)
  {
    ip_addr_t ipaddr = ppp_netif.ip_addr;
    ip_addr_t netmask = ppp_netif.netmask;
    ip_addr_t gw = ppp_netif.gw;
    snprintf(buf, sizeof(buf), "IP=%s NM=%s GW=%s\r\n",
             ipaddr_ntoa(&ipaddr), ipaddr_ntoa(&netmask), ipaddr_ntoa(&gw));
    PPPOS_DebugPrint(buf);
  }
}

static void PPPOS_UART_RxStart(void)
{
  if (pppos_huart != NULL)
  {
    HAL_UART_Receive_IT(pppos_huart, &uart_rx_byte, 1);
  }
}

void PPPOS_Init(UART_HandleTypeDef *huart)
{
  pppos_huart = huart;
  PPPOS_UART_RxStart();

  ppp = pppapi_pppos_create(&ppp_netif, pppos_output_cb, ppp_status_cb, NULL);
  if (ppp == NULL)
  {
    PPPOS_DebugPrint("PPPOS: create failed\r\n");
    return;
  }

  pppapi_set_default(ppp);
  ppp_set_usepeerdns(ppp, 1);
  pppapi_connect(ppp, 0);
}

void PPPOS_Process(void)
{
  /* Required in no-OS lwIP: drive lwIP timers.
     After adding lwIP sources, ensure sys_check_timeouts() is available. */
#if LWIP_TIMERS
  extern void sys_check_timeouts(void);
  sys_check_timeouts();
#endif
}

uint8_t PPPOS_IsLinkUp(void)
{
  return ppp_link_up;
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
  if (huart == pppos_huart)
  {
    if (ppp != NULL)
    {
      pppos_input_byte(ppp, uart_rx_byte);
    }
    PPPOS_UART_RxStart();
  }
}
