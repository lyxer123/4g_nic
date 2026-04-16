#include "uart_ppp.h"
#include "main.h"
#include "lwip/pppapi.h"

extern UART_HandleTypeDef huart2;

static uint8_t uart_rx_buffer[256];

HAL_StatusTypeDef uart_ppp_init(void)
{
    if (HAL_UART_Receive_IT(&huart2, uart_rx_buffer, sizeof(uart_rx_buffer)) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

HAL_StatusTypeDef uart_ppp_send(const uint8_t *data, uint32_t len)
{
    if (HAL_UART_Transmit(&huart2, (uint8_t *)data, len, 1000) != HAL_OK) {
        return HAL_ERROR;
    }
    return HAL_OK;
}

void uart_ppp_poll(void)
{
    // 这个示例使用中断方式接收，必要时可在这里处理状态机。
}

extern ppp_pcb *ppp;

void uart_ppp_receive_callback(uint8_t *data, uint16_t len)
{
    if (ppp != NULL) {
        pppos_input_tcpip(ppp, data, len);
    }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart == &huart2) {
        uart_ppp_receive_callback(uart_rx_buffer, sizeof(uart_rx_buffer));
        HAL_UART_Receive_IT(&huart2, uart_rx_buffer, sizeof(uart_rx_buffer));
    }
}
