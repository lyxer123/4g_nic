#include "pic18_ppp.h"
#include <stdio.h>

// 这个示例为 PIC18 UART PPP 传输层骨架。
// 真实 PPP 客户端需引入 PPP 协议栈并在 PicPpp_HandleRx 中输入接收数据。

#define PPP_BAUD 115200

void PicPpp_Init(void)
{
    // 下面代码仅为示意，请根据具体 PIC 芯片和 XC8 初始化 UART。
    TXSTA = 0x24; // BRGH=1, TX enable
    RCSTA = 0x90; // SPEN=1, CREN=1
    SPBRG = (_XTAL_FREQ / 4 / PPP_BAUD - 1);
    BAUDCON = 0x08; // 16-bit Baud Rate Generator, WUE=0
}

void PicPpp_Task(void)
{
    if (RCIF) {
        uint8_t b = RCREG;
        PicPpp_HandleRx(b);
    }

    // TODO: 在这里从 PPP 协议栈读取待发送数据并调用 PicPpp_Send
}

void PicPpp_Send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (!TXIF) {
        }
        TXREG = data[i];
    }
}

void PicPpp_HandleRx(uint8_t b)
{
    // TODO: 把接收的字节交给 PPP 协议栈。
    // 例如：ppp_input_byte(b);
    (void)b;
}
