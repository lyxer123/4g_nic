/* lwIP options for STM32F103C8T6 + PPPoS minimal configuration */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

/* System options */
#define NO_SYS                          1
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        (16 * 1024)
#define MEMP_MEM_MALLOC                 0
#define MEMP_NUM_PBUF                   16
#define PBUF_POOL_SIZE                  8
#define PBUF_POOL_BUFSIZE               512
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         2
#define MEMP_NUM_TCP_SEG                16
#define MEMP_NUM_SYS_TIMEOUT            20

/* Protocol options */
#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        0
#define ETHARP_SUPPORT_STATIC_ENTRIES   0
#define LWIP_ICMP                       1
#define LWIP_TCP                        1
#define LWIP_UDP                        0
#define LWIP_RAW                        1
#define LWIP_DNS                        1
#define DNS_TABLE_SIZE                  2
#define DNS_MAX_SERVERS                 2

/* Socket options */
#define LWIP_SOCKET                     1
#define LWIP_NETCONN                    0
#define LWIP_COMPAT_SOCKETS             1
#define LWIP_POSIX_SOCKETS_IO_NAMES     0
#define LWIP_SO_RCVTIMEO                1
#define LWIP_SO_RCVBUF                  1
#define LWIP_TCP_KEEPALIVE              0

/* PPP options */
#define PPP_SUPPORT                     1
#define PPPOS_SUPPORT                   1
#define PPPOE_SUPPORT                   0
#define PPP_SUPPORT_MSCHAPv2            0
#define PPP_SUPPORT_ADDRS               1
#define PPP_NETMASK                     0xFFFFFF00UL
#define PPP_MAXIDLEFLAG                 0

/* TCP options */
#define TCP_MSS                         536
#define TCP_SND_BUF                     (2 * TCP_MSS)
#define TCP_WND                         (2 * TCP_MSS)
#define TCP_QUEUE_OOSEQ                 0
#define TCP_OVERSIZE                    0
#define TCP_MAXRTX                      12
#define TCP_SYNMAXRTX                   4
#define TCPIP_THREAD_STACKSIZE          0

/* Misc */
#define LWIP_TIMERS                     1
#define SYS_LIGHTWEIGHT_PROT            0
#define LWIP_PROVIDE_ERRNO              1
#define MEM_STATS                       0
#define LWIP_STATS                      0
#define LWIP_CHECKSUM_CTRL_PER_NETIF    0

#endif /* __LWIPOPTS_H__ */
