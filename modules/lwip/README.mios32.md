# lwIP for MIOS32

This directory vendors the upstream lwIP `STABLE-2_2_1_RELEASE` source tree.
The release archive was downloaded from the official lwIP GitHub mirror:

`https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_1_RELEASE.tar.gz`

SHA-256: `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539`

Upstream files under `src/` are unmodified. MIOS32 integration is kept in
`port/`, `mios32/`, and `lwip.mk` so a future vendor refresh can replace the
upstream tree independently.

The SEQ V4 profile uses lwIP's raw API with `NO_SYS=1`; the existing MIOS32
network task serializes all stack calls. Only Ethernet, ARP, IPv4, ICMP, DHCP,
and UDP are enabled. TCP, IPv6, DNS, sockets, and netconn are excluded.

The receive path wraps a single driver-owned frame buffer in a custom pbuf and
does not allocate packet-pool memory. The LPC17xx build places its 768-byte lwIP
heap in AHB RAM and retains the application's existing 1024-byte EMAC frame
limit. Changes to these values require link-map review and board stress tests.
