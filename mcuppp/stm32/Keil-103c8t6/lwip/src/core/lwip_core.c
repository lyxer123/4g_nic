/*
 * Minimal placeholder for lwIP core sources.
 * Replace this with actual lwIP core source files.
 */

#include "lwip/opt.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include <stdio.h>
#include <string.h>

const char *ipaddr_ntoa(const ip_addr_t *addr)
{
  static char str[16];
  uint32_t a = addr->addr;
  snprintf(str, sizeof(str), "%u.%u.%u.%u",
           (unsigned)((a >> 24) & 0xFF),
           (unsigned)((a >> 16) & 0xFF),
           (unsigned)((a >> 8) & 0xFF),
           (unsigned)(a & 0xFF));
  return str;
}

int ipaddr_aton(const char *cp, ip_addr_t *addr)
{
  unsigned int b0, b1, b2, b3;
  if (sscanf(cp, "%u.%u.%u.%u", &b0, &b1, &b2, &b3) != 4)
  {
    return 0;
  }
  addr->addr = ((b0 & 0xFFU) << 24) | ((b1 & 0xFFU) << 16) | ((b2 & 0xFFU) << 8) | (b3 & 0xFFU);
  return 1;
}

uint16_t inet_chksum(const void *dataptr, size_t len)
{
  const uint16_t *buf = (const uint16_t *)dataptr;
  uint32_t sum = 0;

  while (len > 1)
  {
    sum += *buf++;
    len -= 2;
  }
  if (len == 1)
  {
    sum += *((const uint8_t *)buf);
  }

  while (sum >> 16)
  {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return (uint16_t)~sum;
}
