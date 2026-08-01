#ifndef _LWIP_TASK_H
#define _LWIP_TASK_H

#include <mios32.h>
#include <FreeRTOS.h>
#include <semphr.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LWIP_TASK_STACK_SIZE
# define LWIP_TASK_STACK_SIZE MIOS32_MINIMAL_STACK_SIZE
#endif

#ifndef MY_IP_ADDRESS
# define MY_IP_ADDRESS ((192UL << 24) | (168UL << 16) | (1UL << 8) | 180UL)
#endif
#ifndef MY_NETMASK
# define MY_NETMASK ((255UL << 24) | (255UL << 16) | (255UL << 8))
#endif
#ifndef MY_GATEWAY
# define MY_GATEWAY ((192UL << 24) | (168UL << 16) | (1UL << 8) | 1UL)
#endif

#define UDP_MONITOR_LEVEL_0_OFF              0
#define UDP_MONITOR_LEVEL_1_OSC_REC          1
#define UDP_MONITOR_LEVEL_2_OSC_REC_AND_SEND 2
#define UDP_MONITOR_LEVEL_3_ALL_GEQ_1024     3
#define UDP_MONITOR_LEVEL_4_ALL              4
#define UDP_MONITOR_SEND                     0
#define UDP_MONITOR_RECEIVED                 1

#ifndef LWIP_TASK_MUTEX_MIDIOUT_TAKE
# define LWIP_TASK_MUTEX_MIDIOUT_TAKE { }
#endif
#ifndef LWIP_TASK_MUTEX_MIDIOUT_GIVE
# define LWIP_TASK_MUTEX_MIDIOUT_GIVE { }
#endif
#ifndef LWIP_TASK_MUTEX_MIDIIN_TAKE
# define LWIP_TASK_MUTEX_MIDIIN_TAKE { }
#endif
#ifndef LWIP_TASK_MUTEX_MIDIIN_GIVE
# define LWIP_TASK_MUTEX_MIDIIN_GIVE { }
#endif

#define MUTEX_LWIP_TAKE { while( xSemaphoreTakeRecursive(xLWIPSemaphore, pdMS_TO_TICKS(1U)) != pdTRUE ); }
#define MUTEX_LWIP_GIVE { xSemaphoreGiveRecursive(xLWIPSemaphore); }

extern s32 LWIP_TASK_Init(u32 mode);
extern s32 LWIP_TASK_InitFromPresets(u8 dhcp_enabled, u32 ip, u32 netmask, u32 gateway);
extern s32 LWIP_TASK_NetworkDeviceAvailable(void);
extern s32 LWIP_TASK_ServicesRunning(void);
extern s32 LWIP_TASK_DHCP_EnableSet(u8 enabled);
extern s32 LWIP_TASK_DHCP_EnableGet(void);
extern s32 LWIP_TASK_IP_AddressSet(u32 ip);
extern s32 LWIP_TASK_IP_AddressGet(void);
extern s32 LWIP_TASK_IP_EffectiveAddressGet(void);
extern s32 LWIP_TASK_NetmaskSet(u32 mask);
extern s32 LWIP_TASK_NetmaskGet(void);
extern s32 LWIP_TASK_EffectiveNetmaskGet(void);
extern s32 LWIP_TASK_GatewaySet(u32 ip);
extern s32 LWIP_TASK_GatewayGet(void);
extern s32 LWIP_TASK_EffectiveGatewayGet(void);
extern s32 LWIP_TASK_UDP_MonitorLevelSet(u8 level);
extern s32 LWIP_TASK_UDP_MonitorLevelGet(void);
extern s32 LWIP_TASK_UDP_MonitorPacket(u8 received, const char *prefix,
                                      u32 remote_ip, u16 remote_port,
                                      u16 local_port, const u8 *payload, u32 len);
extern s32 LWIP_TASK_UDP_ESP8266_MonitorPacket(u8 received, char *prefix, u32 ip,
                                               u16 port, u8 *payload, u32 len,
                                               u16 port_local);
extern const u8 *LWIP_TASK_MAC_AddressGet(void);

extern SemaphoreHandle_t xLWIPSemaphore;

/* Source compatibility for applications using the original MIOS32 names. */
#define UIP_TASK_Init                         LWIP_TASK_Init
#define UIP_TASK_InitFromPresets              LWIP_TASK_InitFromPresets
#define UIP_TASK_NetworkDeviceAvailable       LWIP_TASK_NetworkDeviceAvailable
#define UIP_TASK_ServicesRunning              LWIP_TASK_ServicesRunning
#define UIP_TASK_DHCP_EnableSet               LWIP_TASK_DHCP_EnableSet
#define UIP_TASK_DHCP_EnableGet               LWIP_TASK_DHCP_EnableGet
#define UIP_TASK_IP_AddressSet                LWIP_TASK_IP_AddressSet
#define UIP_TASK_IP_AddressGet                LWIP_TASK_IP_AddressGet
#define UIP_TASK_IP_EffectiveAddressGet       LWIP_TASK_IP_EffectiveAddressGet
#define UIP_TASK_NetmaskSet                   LWIP_TASK_NetmaskSet
#define UIP_TASK_NetmaskGet                   LWIP_TASK_NetmaskGet
#define UIP_TASK_EffectiveNetmaskGet          LWIP_TASK_EffectiveNetmaskGet
#define UIP_TASK_GatewaySet                   LWIP_TASK_GatewaySet
#define UIP_TASK_GatewayGet                   LWIP_TASK_GatewayGet
#define UIP_TASK_EffectiveGatewayGet          LWIP_TASK_EffectiveGatewayGet
#define UIP_TASK_UDP_MonitorLevelSet          LWIP_TASK_UDP_MonitorLevelSet
#define UIP_TASK_UDP_MonitorLevelGet          LWIP_TASK_UDP_MonitorLevelGet
#define UIP_TASK_UDP_ESP8266_MonitorPacket    LWIP_TASK_UDP_ESP8266_MonitorPacket
#define UIP_TASK_MUTEX_MIDIOUT_TAKE           LWIP_TASK_MUTEX_MIDIOUT_TAKE
#define UIP_TASK_MUTEX_MIDIOUT_GIVE           LWIP_TASK_MUTEX_MIDIOUT_GIVE
#define UIP_TASK_MUTEX_MIDIIN_TAKE            LWIP_TASK_MUTEX_MIDIIN_TAKE
#define UIP_TASK_MUTEX_MIDIIN_GIVE            LWIP_TASK_MUTEX_MIDIIN_GIVE
#define MUTEX_UIP_TAKE                        MUTEX_LWIP_TAKE
#define MUTEX_UIP_GIVE                        MUTEX_LWIP_GIVE

#ifdef __cplusplus
}
#endif

#endif
