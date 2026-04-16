/* lwIP options header placeholder */
#ifndef LWIP_HDR_OPT_H
#define LWIP_HDR_OPT_H

#include <stdint.h>
#include "lwipopts.h"
#include "lwip/sys.h"

typedef uint8_t u8_t;
typedef uint16_t u16_t;
typedef uint32_t u32_t;

typedef int err_t;

static inline uint16_t lwip_htons(uint16_t x)
{
  return (uint16_t)((x << 8) | (x >> 8));
}

static inline uint16_t htons(uint16_t x)
{
  return lwip_htons(x);
}

#endif /* LWIP_HDR_OPT_H */
