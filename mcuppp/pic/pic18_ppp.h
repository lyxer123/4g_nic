#ifndef PIC18_PPP_H
#define PIC18_PPP_H

#include <xc.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void PicPpp_Init(void);
void PicPpp_Task(void);
void PicPpp_Send(const uint8_t *data, uint16_t len);
void PicPpp_HandleRx(uint8_t b);

#ifdef __cplusplus
}
#endif

#endif // PIC18_PPP_H
