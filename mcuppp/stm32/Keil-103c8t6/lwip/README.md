# lwIP Integration for pppos Project

This project is prepared to use lwIP with PPPoS on STM32F103C8.

## What is already configured
- `Core/Inc/lwipopts.h`: minimal lwIP configuration for `NO_SYS=1`, PPPoS, socket and DNS support.
- `Core/Src/pppos_lwip.c`: PPPoS helper using `pppapi_pppos_create()` and UART interrupt input.
- `Core/Src/lwip_app.c`: sample `LWIP_PingTest()` and `LWIP_HttpGetTest()`.
- `MDK-ARM/pppos.uvprojx`: include path updated with `../lwip/src/include` and `../lwip/src/include/ppp`; `pppos_lwip.c` and `lwip_app.c` added to project.

## What you still need to add
1. Download and extract the lwIP source tree into `lwip/src`.
   - Required directories: `core`, `api`, `netif`, `include`, `src/netif/ppp`.
2. Make sure the project include path points to `../lwip/src/include` and `../lwip/src/include/ppp`.
3. Confirm `Core/Inc/lwipopts.h` is included by the lwIP build.

## Suggested lwIP folders
- `lwip/src/core`
- `lwip/src/api`
- `lwip/src/netif`
- `lwip/src/netif/ppp`
- `lwip/src/include`

## Notes
- `lwipopts.h` is already configured for minimal RAM usage on STM32F103.
- `main.c` will call `LWIP_HttpGetTest("www.baidu.com", "/")` and `LWIP_PingTest("8.8.8.8")` once the PPP link is up.
- If you use DHCP or dynamic DNS, adjust `lwipopts.h` accordingly.
