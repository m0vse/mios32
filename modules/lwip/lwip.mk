# lwIP 2.2.1, configured for the MIOS32 single-threaded network task.

C_INCLUDE += \
	-I $(MIOS32_PATH)/modules/lwip/port \
	-I $(MIOS32_PATH)/modules/lwip/src/include \
	-I $(MIOS32_PATH)/modules/lwip/mios32/$(MIOS32_FAMILY)

THUMB_SOURCE += \
	$(MIOS32_PATH)/modules/lwip/src/core/init.c \
	$(MIOS32_PATH)/modules/lwip/src/core/def.c \
	$(MIOS32_PATH)/modules/lwip/src/core/inet_chksum.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ip.c \
	$(MIOS32_PATH)/modules/lwip/src/core/mem.c \
	$(MIOS32_PATH)/modules/lwip/src/core/memp.c \
	$(MIOS32_PATH)/modules/lwip/src/core/netif.c \
	$(MIOS32_PATH)/modules/lwip/src/core/pbuf.c \
	$(MIOS32_PATH)/modules/lwip/src/core/stats.c \
	$(MIOS32_PATH)/modules/lwip/src/core/sys.c \
	$(MIOS32_PATH)/modules/lwip/src/core/timeouts.c \
	$(MIOS32_PATH)/modules/lwip/src/core/udp.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ipv4/acd.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ipv4/dhcp.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ipv4/etharp.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ipv4/icmp.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ipv4/ip4.c \
	$(MIOS32_PATH)/modules/lwip/src/core/ipv4/ip4_addr.c \
	$(MIOS32_PATH)/modules/lwip/src/netif/ethernet.c \
	$(MIOS32_PATH)/modules/lwip/mios32/sys_arch.c \
	$(MIOS32_PATH)/modules/lwip/mios32/$(MIOS32_FAMILY)/network-device.c

ifeq ($(FAMILY),LPC17xx)
THUMB_SOURCE += $(MIOS32_PATH)/modules/lwip/mios32/$(MIOS32_FAMILY)/lpc17xx_emac.c
endif

DIST += $(MIOS32_PATH)/modules/lwip
