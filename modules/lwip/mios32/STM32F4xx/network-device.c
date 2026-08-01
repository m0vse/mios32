// $Id$
/*
 * Access functions to network device
 *
 * ==========================================================================
 *
 *  Copyright (C) 2009 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 *
 * ==========================================================================
 */

#include <mios32.h>
#include "network-device.h"


/////////////////////////////////////////////////////////////////////////////
// for optional debugging messages via DEBUG_MSG (defined in mios32_config.h)
/////////////////////////////////////////////////////////////////////////////

#define DEBUG_VERBOSE_LEVEL 0


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////
static u8 netdev_available;


/////////////////////////////////////////////////////////////////////////////
// Network Device Functions
/////////////////////////////////////////////////////////////////////////////

void network_device_init(void)
{
  s32 status;

  status = MIOS32_ENC28J60_Init(0);
  netdev_available = (status >= 0);
#if DEBUG_VERBOSE_LEVEL >= 1
  MIOS32_MIDI_SendDebugMessage("[network_device_init] status %d, available: %d\n", status, netdev_available);
  if( status >= 0 )
    MIOS32_MIDI_SendDebugMessage("[network_device_init] ENC28J60 RevID: 0x%02x\n", MIOS32_ENC28J60_RevIDGet());
#endif
}

void network_device_check(void)
{
  u8 prev_netdev_available = netdev_available;
  netdev_available = MIOS32_ENC28J60_CheckAvailable(prev_netdev_available);

  if( netdev_available && !prev_netdev_available ) {
    MIOS32_MIDI_SendDebugMessage("[network_device_check] ENC28J60 has been connected, RevID: 0x%02x\n", MIOS32_ENC28J60_RevIDGet());
  } else if( !netdev_available && prev_netdev_available ) {
    MIOS32_MIDI_SendDebugMessage("[network_device_check] ENC28J60 has been disconnected\n");
  }
}

int network_device_available(void)
{
  return netdev_available;
}

int network_device_read(unsigned char *buffer, unsigned int buffer_size)
{
  s32 status;

  if( (status=MIOS32_ENC28J60_PackageReceive((u8 *)buffer, buffer_size)) < 0 ) {
    netdev_available = 0;
#if DEBUG_VERBOSE_LEVEL >= 1
    MIOS32_MIDI_SendDebugMessage("[network_device_read] ERROR %d\n", status);
#endif
    return 0;
  }

#if DEBUG_VERBOSE_LEVEL >= 2
  if( status ) {
    MIOS32_MIDI_SendDebugMessage("[network_device_read] received %d bytes\n", status);
  }
#endif

#if DEBUG_VERBOSE_LEVEL >= 3
  if( status ) {
    MIOS32_MIDI_SendDebugHexDump((u8 *)buffer, status);
  }
#endif

  return status;
}

int network_device_send(const unsigned char *buffer, unsigned int len,
                        const unsigned char *buffer2, unsigned int len2)
{
  s32 status = MIOS32_ENC28J60_PackageSend((u8 *)buffer, len, (u8 *)buffer2, len2);

  if( status < 0 ) {
    netdev_available = 0;
#if DEBUG_VERBOSE_LEVEL >= 1
    MIOS32_MIDI_SendDebugMessage("[network_device_send] ERROR %d\n", status);
#endif
  } else {
#if DEBUG_VERBOSE_LEVEL >= 2
    if( status ) {
      MIOS32_MIDI_SendDebugMessage("[network_device_send] sent %d bytes\n", len + len2);
    }
#endif
  }

  return status;
}


unsigned char *network_device_mac_addr(void)
{
  return (unsigned char *)MIOS32_ENC28J60_MAC_AddrGet();
}
