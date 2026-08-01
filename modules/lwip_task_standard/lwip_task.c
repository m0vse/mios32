#include <mios32.h>

#if !defined(MIOS32_DONT_USE_OSC)

#include <FreeRTOS.h>
#include <task.h>

#include "lwip/init.h"
#include "lwip/dhcp.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

#include "network-device.h"
#include "lwip_task.h"
#include "osc_server.h"
#include "osc_client.h"

#if OSC_SERVER_ESP8266_ENABLED
#include <esp8266.h>
#endif

#define DEBUG_VERBOSE_LEVEL 1
#ifndef DEBUG_MSG
# define DEBUG_MSG MIOS32_MIDI_SendDebugMessage
#endif

#define PRIORITY_TASK_LWIP (tskIDLE_PRIORITY + 3)
#if defined(MIOS32_FAMILY_LPC17xx)
# define LWIP_TASK_FRAME_SIZE LPC17XX_EMAC_FRAG_SIZE
#else
# define LWIP_TASK_FRAME_SIZE MIOS32_ENC28J60_MAX_FRAME_SIZE
#endif

SemaphoreHandle_t xLWIPSemaphore;

static struct netif ethernet_netif;
static struct pbuf_custom rx_pbuf;
static u8 rx_buffer[LWIP_TASK_FRAME_SIZE] __attribute__((aligned(4)));
static u8 services_running;
static u8 dhcp_enabled = 1;
static u8 udp_monitor_level;
static u32 my_ip_address = MY_IP_ADDRESS;
static u32 my_netmask = MY_NETMASK;
static u32 my_gateway = MY_GATEWAY;

static void LWIP_TASK_Handler(void *pvParameters);
static s32 LWIP_TASK_StartServices(void);
static s32 LWIP_TASK_StopServices(void);

static ip4_addr_t LWIP_TASK_IPFromPacked(u32 address)
{
  ip4_addr_t result;
  IP4_ADDR(&result, address >> 24, address >> 16, address >> 8, address);
  return result;
}

static u32 LWIP_TASK_IPToPacked(const ip4_addr_t *address)
{
  return ((u32)ip4_addr1(address) << 24) | ((u32)ip4_addr2(address) << 16) |
         ((u32)ip4_addr3(address) << 8) | (u32)ip4_addr4(address);
}

static void LWIP_TASK_CustomPbufFree(struct pbuf *p)
{
  (void)p;
}

static err_t LWIP_TASK_LinkOutput(struct netif *netif, struct pbuf *p)
{
  struct pbuf *flat = NULL;
  const struct pbuf *packet = p;
  const u8 *buffer2 = NULL;
  u16 len2 = 0;
  s32 status;

  (void)netif;
  if( p->next != NULL && p->next->next != NULL ) {
    flat = pbuf_clone(PBUF_RAW, PBUF_RAM, p);
    if( flat == NULL )
      return ERR_MEM;
    packet = flat;
  }

  if( packet->next != NULL ) {
    buffer2 = packet->next->payload;
    len2 = packet->next->len;
  }
  status = network_device_send(packet->payload, packet->len, buffer2, len2);
  if( flat != NULL )
    pbuf_free(flat);
  return status < 0 ? ERR_IF : ERR_OK;
}

static err_t LWIP_TASK_NetifInit(struct netif *netif)
{
  const u8 *mac = network_device_mac_addr();
  int i;

  netif->name[0] = 'm';
  netif->name[1] = 'b';
  netif->output = etharp_output;
  netif->linkoutput = LWIP_TASK_LinkOutput;
  netif->mtu = 1500;
  netif->hwaddr_len = ETH_HWADDR_LEN;
  for(i=0; i<ETH_HWADDR_LEN; ++i)
    netif->hwaddr[i] = mac[i];
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
  return ERR_OK;
}

static void LWIP_TASK_StatusChanged(struct netif *netif)
{
  if( netif_is_link_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif)) )
    LWIP_TASK_StartServices();
  else
    LWIP_TASK_StopServices();
}

s32 LWIP_TASK_Init(u32 mode)
{
  if( mode > 0 )
    return -1;

  OSC_CLIENT_Init(0);
  xLWIPSemaphore = xSemaphoreCreateRecursiveMutex();
  if( xLWIPSemaphore == NULL )
    return -2;

  services_running = 0;
  udp_monitor_level = UDP_MONITOR_LEVEL_0_OFF;
  if( xTaskCreate(LWIP_TASK_Handler, "lwIP", LWIP_TASK_STACK_SIZE/4, NULL,
                  PRIORITY_TASK_LWIP, NULL) != pdPASS )
    return -3;

#if OSC_SERVER_ESP8266_ENABLED
  ESP8266_Init(0);
  ESP8266_InitUart(UART2, 115200);
#endif
  return 0;
}

s32 LWIP_TASK_InitFromPresets(u8 enabled, u32 ip, u32 netmask, u32 gateway)
{
  dhcp_enabled = enabled;
  my_ip_address = ip;
  my_netmask = netmask;
  my_gateway = gateway;
  return 0;
}

static void LWIP_TASK_Handler(void *pvParameters)
{
  TickType_t last_execution;
  u8 link_was_up;

  (void)pvParameters;
  MUTEX_LWIP_TAKE;
  network_device_init();
  lwip_init();
  netif_add_noaddr(&ethernet_netif, NULL, LWIP_TASK_NetifInit, ethernet_input);
  netif_set_default(&ethernet_netif);
  netif_set_status_callback(&ethernet_netif, LWIP_TASK_StatusChanged);
  netif_set_link_callback(&ethernet_netif, LWIP_TASK_StatusChanged);
  netif_set_up(&ethernet_netif);
  link_was_up = network_device_available() != 0;
  if( link_was_up )
    netif_set_link_up(&ethernet_netif);
  else
    netif_set_link_down(&ethernet_netif);
  LWIP_TASK_DHCP_EnableSet(dhcp_enabled);
  MUTEX_LWIP_GIVE;

  last_execution = xTaskGetTickCount();
  while( 1 ) {
    int frame_len;
    u8 link_is_up;

    xTaskDelayUntil(&last_execution, pdMS_TO_TICKS(1U));
    MUTEX_LWIP_TAKE;

    if( !(MIOS32_TIMESTAMP_Get() % 100U) )
      network_device_check();
    link_is_up = network_device_available() != 0;
    if( link_is_up != link_was_up ) {
      link_was_up = link_is_up;
      if( link_is_up )
        netif_set_link_up(&ethernet_netif);
      else {
        netif_set_link_down(&ethernet_netif);
        LWIP_TASK_StopServices();
      }
    }

    if( link_is_up ) {
      frame_len = network_device_read(rx_buffer, sizeof(rx_buffer));
      if( frame_len > 0 ) {
        rx_pbuf.custom_free_function = LWIP_TASK_CustomPbufFree;
        struct pbuf *p = pbuf_alloced_custom(PBUF_RAW, frame_len, PBUF_REF,
                                             &rx_pbuf, rx_buffer, sizeof(rx_buffer));
        if( p != NULL && ethernet_input(p, &ethernet_netif) != ERR_OK )
          pbuf_free(p);
      }
    }
    sys_check_timeouts();
    MUTEX_LWIP_GIVE;

#if OSC_SERVER_ESP8266_ENABLED
    ESP8266_Periodic_mS();
#endif
  }
}

s32 LWIP_TASK_DHCP_EnableSet(u8 enabled)
{
  dhcp_enabled = enabled != 0;
  LWIP_TASK_StopServices();
  if( dhcp_enabled ) {
    ip4_addr_t zero = *IP4_ADDR_ANY4;
    netif_set_addr(&ethernet_netif, &zero, &zero, &zero);
    return dhcp_start(&ethernet_netif) == ERR_OK ? 0 : -1;
  } else {
    ip4_addr_t ip = LWIP_TASK_IPFromPacked(my_ip_address);
    ip4_addr_t mask = LWIP_TASK_IPFromPacked(my_netmask);
    ip4_addr_t gateway = LWIP_TASK_IPFromPacked(my_gateway);
    dhcp_release_and_stop(&ethernet_netif);
    netif_set_addr(&ethernet_netif, &ip, &mask, &gateway);
    return netif_is_link_up(&ethernet_netif) ? LWIP_TASK_StartServices() : 0;
  }
}

s32 LWIP_TASK_DHCP_EnableGet(void) { return dhcp_enabled; }

s32 LWIP_TASK_IP_AddressSet(u32 ip)
{
  my_ip_address = ip;
  if( !dhcp_enabled ) {
    ip4_addr_t value = LWIP_TASK_IPFromPacked(ip);
    netif_set_ipaddr(&ethernet_netif, &value);
  }
  return 0;
}

s32 LWIP_TASK_IP_AddressGet(void) { return my_ip_address; }
s32 LWIP_TASK_IP_EffectiveAddressGet(void) { return LWIP_TASK_IPToPacked(netif_ip4_addr(&ethernet_netif)); }

s32 LWIP_TASK_NetmaskSet(u32 mask)
{
  my_netmask = mask;
  if( !dhcp_enabled ) {
    ip4_addr_t value = LWIP_TASK_IPFromPacked(mask);
    netif_set_netmask(&ethernet_netif, &value);
  }
  return 0;
}

s32 LWIP_TASK_NetmaskGet(void) { return my_netmask; }
s32 LWIP_TASK_EffectiveNetmaskGet(void) { return LWIP_TASK_IPToPacked(netif_ip4_netmask(&ethernet_netif)); }

s32 LWIP_TASK_GatewaySet(u32 ip)
{
  my_gateway = ip;
  if( !dhcp_enabled ) {
    ip4_addr_t value = LWIP_TASK_IPFromPacked(ip);
    netif_set_gw(&ethernet_netif, &value);
  }
  return 0;
}

s32 LWIP_TASK_GatewayGet(void) { return my_gateway; }
s32 LWIP_TASK_EffectiveGatewayGet(void) { return LWIP_TASK_IPToPacked(netif_ip4_gw(&ethernet_netif)); }

static s32 LWIP_TASK_StartServices(void)
{
  if( services_running )
    return 0;
#if DEBUG_VERBOSE_LEVEL >= 1
  u32 ip = LWIP_TASK_IP_EffectiveAddressGet();
  LWIP_TASK_MUTEX_MIDIOUT_TAKE;
  DEBUG_MSG("[lwIP] IP address: %d.%d.%d.%d\n", ip >> 24, (ip >> 16) & 0xff,
            (ip >> 8) & 0xff, ip & 0xff);
  LWIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif
  if( OSC_SERVER_Init(0) < 0 )
    return -1;
  services_running = 1;
  return 0;
}

static s32 LWIP_TASK_StopServices(void)
{
  if( services_running )
    OSC_SERVER_Init(1);
  services_running = 0;
  return 0;
}

s32 LWIP_TASK_ServicesRunning(void) { return services_running; }
s32 LWIP_TASK_NetworkDeviceAvailable(void) { return network_device_available(); }
s32 LWIP_TASK_UDP_MonitorLevelSet(u8 level) { udp_monitor_level = level; return 0; }
s32 LWIP_TASK_UDP_MonitorLevelGet(void) { return udp_monitor_level; }
const u8 *LWIP_TASK_MAC_AddressGet(void) { return network_device_mac_addr(); }

s32 LWIP_TASK_UDP_MonitorPacket(u8 received, const char *prefix, u32 remote_ip,
                                u16 remote_port, u16 local_port,
                                const u8 *payload, u32 len)
{
  LWIP_TASK_MUTEX_MIDIOUT_TAKE;
  DEBUG_MSG(received ? "[UDP:%s] from %d.%d.%d.%d:%d to port %d (%d bytes)\n"
                     : "[UDP:%s] to %d.%d.%d.%d:%d from port %d (%d bytes)\n",
            prefix, remote_ip >> 24, (remote_ip >> 16) & 0xff,
            (remote_ip >> 8) & 0xff, remote_ip & 0xff,
            remote_port, local_port, len);
  MIOS32_MIDI_SendDebugHexDump((u8 *)payload, len);
  LWIP_TASK_MUTEX_MIDIOUT_GIVE;
  return 0;
}

s32 LWIP_TASK_UDP_ESP8266_MonitorPacket(u8 received, char *prefix, u32 ip,
                                        u16 port, u8 *payload, u32 len,
                                        u16 port_local)
{
  u32 packed = ((ip & 0xffU) << 24) | ((ip & 0xff00U) << 8) |
               ((ip & 0xff0000U) >> 8) | ((ip >> 24) & 0xffU);
  return LWIP_TASK_UDP_MonitorPacket(received, prefix, packed, port,
                                     port_local, payload, len);
}

#endif
