#ifndef MIOS32_LWIPOPTS_H
#define MIOS32_LWIPOPTS_H

#define NO_SYS                          1
#define SYS_LIGHTWEIGHT_PROT            0

#define LWIP_IPV4                       1
#define LWIP_IPV6                       0
#define LWIP_ARP                        1
#define LWIP_ETHERNET                   1
#define LWIP_ICMP                       1
#define LWIP_DHCP                       1
#define LWIP_DHCP_DOES_ACD_CHECK        0
#define LWIP_ACD                        0
#define LWIP_AUTOIP                     0
#define LWIP_IGMP                       0
#define IP_FORWARD                      0
#define IP_REASSEMBLY                   0
#define IP_FRAG                         0

#define LWIP_UDP                        1
#define LWIP_TCP                        0
#define LWIP_RAW                        0
#define LWIP_DNS                        0
#define LWIP_NETCONN                    0
#define LWIP_SOCKET                     0

#define LWIP_TIMERS                     1
#define LWIP_TIMERS_CUSTOM              0
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1
#define LWIP_SINGLE_NETIF               1

#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        768
#define MEMP_NUM_PBUF                   2
#define MEMP_NUM_UDP_PCB                6
#define MEMP_NUM_SYS_TIMEOUT            8
#define PBUF_POOL_SIZE                  0

#define LWIP_STATS                      0
#define LWIP_DEBUG                      0
#define LWIP_CHECKSUM_CTRL_PER_NETIF    0
#define LWIP_CHKSUM_ALGORITHM           2
#define ETH_PAD_SIZE                    0
#define LWIP_SUPPORT_CUSTOM_PBUF        1

#if defined(MIOS32_FAMILY_LPC17xx)
extern unsigned char mios32_lwip_ram_heap[];
# define LWIP_RAM_HEAP_POINTER mios32_lwip_ram_heap
#endif

#endif
