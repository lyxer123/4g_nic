#ifndef LWIP_HDR_PPP_H
#define LWIP_HDR_PPP_H

#include <stdint.h>

typedef struct ppp_pcb ppp_pcb;

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;

typedef int err_t;

#define PPPERR_NONE            0
#define PPPERR_PARAM           1
#define PPPERR_OPEN            2
#define PPPERR_DEVICE          3
#define PPPERR_ALLOC           4
#define PPPERR_USER            5
#define PPPERR_CONNECT         6
#define PPPERR_ABORT           7
#define PPPERR_PROTOCOL        8
#define PPPERR_PEERDEAD        9
#define PPPERR_IDLETIMEOUT    10
#define PPPERR_CONNECTTIMEOUT 11
#define PPPERR_LOOPBACK       12

#endif /* LWIP_HDR_PPP_H */
