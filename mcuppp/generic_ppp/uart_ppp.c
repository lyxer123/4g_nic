#include "uart_ppp.h"
#include "uart_driver.h"
#include "pppos.h" // Replace with your target's PPPoS UART driver headers

static uint8_t uart_rx_buffer[256];

void uart_ppp_init(void)
{
    uart_driver_init();
    uart_driver_start_receive(uart_rx_buffer, sizeof(uart_rx_buffer));
}

void uart_ppp_poll(void)
{
    int len = uart_driver_read(uart_rx_buffer, sizeof(uart_rx_buffer));
    if (len > 0) {
        pppos_input_tcpip(NULL, uart_rx_buffer, len);
    }
}

uint32_t uart_ppp_output(ppp_pcb *pcb, uint8_t *data, uint32_t len)
{
    return uart_driver_write(data, len) ? len : 0;
}
