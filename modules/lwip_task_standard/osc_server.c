// $Id$
/*
 * OSC daemon/server for lwIP
 *
 * ==========================================================================
 *
 *  Copyright (C) 2009 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 *
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////
#include <mios32.h>
#include <string.h>

#if !defined(MIOS32_DONT_USE_OSC)

#include "lwip/ip_addr.h"
#include "lwip/pbuf.h"
#include "lwip/udp.h"
#include "lwip_task.h"

#include "osc_server.h"
#include "osc_client.h"

#if OSC_SERVER_ESP8266_ENABLED
#include <esp8266.h>
#endif

#include <app.h>

/////////////////////////////////////////////////////////////////////////////
// for optional debugging messages via MIOS32_MIDI_SendDebug*
/////////////////////////////////////////////////////////////////////////////

#define DEBUG_VERBOSE_LEVEL 1

#ifndef DEBUG_MSG
# define DEBUG_MSG MIOS32_MIDI_SendDebugMessage
#endif


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static struct udp_pcb *osc_conn[OSC_SERVER_NUM_CONNECTIONS];

const static mios32_osc_search_tree_t parse_root[];
static u8 osc_parsed_from_con;

// TODO: variable initialisation contains hardcoded dependency to OSC_SERVER_NUM_CONNECTIONS!
static u32 osc_remote_ip[OSC_SERVER_NUM_CONNECTIONS] = { OSC_REMOTE_IP, OSC_REMOTE_IP, OSC_REMOTE_IP, OSC_REMOTE_IP };
static u16 osc_remote_port[OSC_SERVER_NUM_CONNECTIONS] = { OSC_REMOTE_PORT, OSC_REMOTE_PORT, OSC_REMOTE_PORT, OSC_REMOTE_PORT };
static u16 osc_local_port[OSC_SERVER_NUM_CONNECTIONS] = { OSC_LOCAL_PORT, OSC_LOCAL_PORT, OSC_LOCAL_PORT, OSC_LOCAL_PORT };

#if OSC_SERVER_ESP8266_ENABLED
static s32 OSC_SERVER_ESP8266_NotifyUdpPacket(u32 ip, u16 port, u8 *payload, u32 len);
#endif

static void OSC_SERVER_Receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                               const ip_addr_t *address, u16_t port);

static u32 OSC_SERVER_IPToPacked(const ip_addr_t *address)
{
  const ip4_addr_t *ip = ip_2_ip4(address);
  return ((u32)ip4_addr1(ip) << 24) | ((u32)ip4_addr2(ip) << 16) |
         ((u32)ip4_addr3(ip) << 8) | (u32)ip4_addr4(ip);
}

static ip_addr_t OSC_SERVER_IPFromPacked(u32 address)
{
  ip_addr_t result;
  IP_ADDR4(&result, address >> 24, address >> 16, address >> 8, address);
  return result;
}

/////////////////////////////////////////////////////////////////////////////
// Initialize the OSC daemon
/////////////////////////////////////////////////////////////////////////////
s32 OSC_SERVER_Init(u32 mode)
{
  int con;

  // remove open connections
  for(con=0; con<OSC_SERVER_NUM_CONNECTIONS; ++con) {
    if( osc_conn[con] != NULL ) {
      int following;
      struct udp_pcb *pcb = osc_conn[con];
      for(following=con+1; following<OSC_SERVER_NUM_CONNECTIONS; ++following)
        if( osc_conn[following] == pcb )
          osc_conn[following] = NULL;
      udp_remove(pcb);
      osc_conn[con] = NULL;
    }
  }

  if( mode != 0 )
    return 0;

  // create one raw UDP endpoint for each independently configured OSC port
  for(con=0; con<OSC_SERVER_NUM_CONNECTIONS; ++con) {
    int previous;
    for(previous=0; previous<con; ++previous) {
      if( osc_local_port[previous] == osc_local_port[con] ) {
        osc_conn[con] = osc_conn[previous];
        break;
      }
    }
    if( previous < con )
      continue;

    osc_conn[con] = udp_new_ip_type(IPADDR_TYPE_V4);
    if( osc_conn[con] != NULL ) {
      ip_set_option(osc_conn[con], SOF_BROADCAST);
      if( udp_bind(osc_conn[con], IP_ANY_TYPE, osc_local_port[con]) != ERR_OK ) {
        udp_remove(osc_conn[con]);
        osc_conn[con] = NULL;
      } else {
        udp_recv(osc_conn[con], OSC_SERVER_Receive, NULL);
      }
    }

    if( osc_conn[con] != NULL ) {
#if DEBUG_VERBOSE_LEVEL >= 2
      LWIP_TASK_MUTEX_MIDIOUT_TAKE;
      DEBUG_MSG("[OSC_SERVER] #%d listens on port %d\n", con, osc_local_port[con]);
      LWIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif
    } else {
#if DEBUG_VERBOSE_LEVEL >= 1
      LWIP_TASK_MUTEX_MIDIOUT_TAKE;
      DEBUG_MSG("[OSC_SERVER] FAILED to create connection #%d (no free ports)\n", con);
      LWIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif
      return -1;
    }
  }

#if OSC_SERVER_ESP8266_ENABLED
  ESP8266_UdpRxCallback_Init(OSC_SERVER_ESP8266_NotifyUdpPacket); // hook to notify received UDP packets
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Init function for presets (read before OSC_SERVER_Init()
/////////////////////////////////////////////////////////////////////////////
s32 OSC_SERVER_InitFromPresets(u8 con, u32 _osc_remote_ip, u16 _osc_remote_port, u16 _osc_local_port)
{
  if( con >= OSC_SERVER_NUM_CONNECTIONS )
    return -1; // invalid connection

  osc_remote_ip[con] = _osc_remote_ip;
  osc_remote_port[con] = _osc_remote_port;
  osc_local_port[con] = _osc_local_port;

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Get/Set functions
/////////////////////////////////////////////////////////////////////////////
s32 OSC_SERVER_RemoteIP_Set(u8 con, u32 ip)
{
  if( con >= OSC_SERVER_NUM_CONNECTIONS )
    return -1; // invalid connection

  osc_remote_ip[con] = ip;
#if 0
  return OSC_SERVER_Init(0);
#else
  return 0; // OSC_SERVER_Init(0) has to be called after all settings have been done
#endif
}

u32 OSC_SERVER_RemoteIP_Get(u8 con)
{
  return osc_remote_ip[con];
}


s32 OSC_SERVER_RemotePortSet(u8 con, u16 port)
{
  if( con >= OSC_SERVER_NUM_CONNECTIONS )
    return -1; // invalid connection

  osc_remote_port[con] = port;
#if 0
  return OSC_SERVER_Init(0);
#else
  return 0; // OSC_SERVER_Init(0) has to be called after all settings have been done
#endif
}

u16 OSC_SERVER_RemotePortGet(u8 con)
{
  return osc_remote_port[con];
}

s32 OSC_SERVER_LocalPortSet(u8 con, u16 port)
{
  if( con >= OSC_SERVER_NUM_CONNECTIONS )
    return -1; // invalid connection

  osc_local_port[con] = port;

#if OSC_SERVER_ESP8266_ENABLED
  if( ESP8266_UartGet() ) {
    char cmd[80];
    sprintf(cmd, "set udp_port %d %d\n", con+1, port);
    ESP8266_SendCommand(cmd);
  }
#endif

#if 0
  return OSC_SERVER_Init(0);
#else
  return 0; // OSC_SERVER_Init(0) has to be called after all settings have been done
#endif
}

u16 OSC_SERVER_LocalPortGet(u8 con)
{
  return osc_local_port[con];
}


/////////////////////////////////////////////////////////////////////////////
// Compatibility entry point retained for applications that called it directly.
/////////////////////////////////////////////////////////////////////////////
static void OSC_SERVER_Receive(void *arg, struct udp_pcb *pcb, struct pbuf *p,
                               const ip_addr_t *address, u16_t port)
{
  u8 con;
  u32 remote_ip;
  u8 monitor_level;
  s32 status;

  if( p == NULL )
    return;

  (void)arg;
  remote_ip = OSC_SERVER_IPToPacked(address);
  monitor_level = LWIP_TASK_UDP_MonitorLevelGet();
  for(con=0; con<OSC_SERVER_NUM_CONNECTIONS; ++con)
    if( osc_local_port[con] == pcb->local_port &&
        (osc_remote_ip[con] == 0xffffffffUL || osc_remote_ip[con] == remote_ip) )
      break;

  if( con >= OSC_SERVER_NUM_CONNECTIONS ) {
    if( monitor_level >= UDP_MONITOR_LEVEL_4_ALL ||
        (monitor_level >= UDP_MONITOR_LEVEL_3_ALL_GEQ_1024 && pcb->local_port >= 1024) )
      LWIP_TASK_UDP_MonitorPacket(UDP_MONITOR_RECEIVED, "UNMATCHED_SOURCE",
                                  remote_ip, port, pcb->local_port, p->payload, p->tot_len);
    pbuf_free(p);
    return;
  }

  if( monitor_level >= UDP_MONITOR_LEVEL_1_OSC_REC )
    LWIP_TASK_UDP_MonitorPacket(UDP_MONITOR_RECEIVED, "OSC_RECEIVED",
                                remote_ip, port, pcb->local_port, p->payload, p->tot_len);

  osc_parsed_from_con = con;
  status = MIOS32_OSC_ParsePacket((u8 *)p->payload, p->tot_len, parse_root);
#if DEBUG_VERBOSE_LEVEL >= 2
  if( status < 0 ) {
    LWIP_TASK_MUTEX_MIDIOUT_TAKE;
    DEBUG_MSG("[OSC_SERVER] invalid OSC packet, status %d\n", status);
    LWIP_TASK_MUTEX_MIDIOUT_GIVE;
  }
#else
  (void)status;
#endif
  pbuf_free(p);
}

s32 OSC_SERVER_AppCall(void)
{
  return 0;
}

s32 OSC_SERVER_SendPacket(u8 con, u8 *packet, u32 len)
{
  struct pbuf *p;
  ip_addr_t destination;
  err_t status;

  if( len == 0 )
    return 0;
  if( con >= OSC_SERVER_NUM_CONNECTIONS || osc_conn[con] == NULL )
    return -1;

#if OSC_SERVER_ESP8266_ENABLED
  if( ESP8266_UartGet() ) {
    u32 ip = ((osc_remote_ip[con] & 0xff000000UL) >> 24) |
             ((osc_remote_ip[con] & 0x00ff0000UL) >> 8) |
             ((osc_remote_ip[con] & 0x0000ff00UL) << 8) |
             ((osc_remote_ip[con] & 0x000000ffUL) << 24);
    return ESP8266_COM_SendUdpPacket(ip, osc_remote_port[con], packet, len);
  }
#endif

  MUTEX_LWIP_TAKE;
  p = pbuf_alloc(PBUF_TRANSPORT, len, PBUF_RAM);
  if( p == NULL ) {
    MUTEX_LWIP_GIVE;
    return -2;
  }
  if( pbuf_take(p, packet, len) != ERR_OK ) {
    pbuf_free(p);
    MUTEX_LWIP_GIVE;
    return -3;
  }

  destination = OSC_SERVER_IPFromPacked(osc_remote_ip[con]);
  status = udp_sendto(osc_conn[con], p, &destination, osc_remote_port[con]);
  if( LWIP_TASK_UDP_MonitorLevelGet() >= UDP_MONITOR_LEVEL_2_OSC_REC_AND_SEND )
    LWIP_TASK_UDP_MonitorPacket(UDP_MONITOR_SEND, "OSC_SEND", osc_remote_ip[con],
                                osc_remote_port[con], osc_local_port[con], packet, len);
  pbuf_free(p);
  MUTEX_LWIP_GIVE;
  return status == ERR_OK ? 0 : -4;
}


/////////////////////////////////////////////////////////////////////////////
// Method to send a MIDI message
// Path: /midi <midi-package>
/////////////////////////////////////////////////////////////////////////////
static s32 OSC_SERVER_Method_MIDI(mios32_osc_args_t *osc_args, u32 method_arg)
{
#if DEBUG_VERBOSE_LEVEL >= 2
  UIP_TASK_MUTEX_MIDIOUT_TAKE;
  MIOS32_OSC_SendDebugMessage(osc_args, method_arg);
  UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

  // check osc port
  u8 con = method_arg & 0xf;
  if( con > OSC_SERVER_NUM_CONNECTIONS )
    return -1; // wrong port

  // we expect at least 1 argument
  if( osc_args->num_args < 1 )
    return -2; // wrong number of arguments

  // check for MIDI event
  if( osc_args->arg_type[0] == 'm' ) {
    mios32_midi_package_t p = MIOS32_OSC_GetMIDI(osc_args->arg_ptr[0]);

    // extra treatment for SysEx messages
    // Note: SysEx streams will be sent as blobs and therefore don't need to be considered here
    if( p.evnt0 >= 0xf0 || p.evnt0 < 0x80 ) {
      if( p.evnt0 >= 0xf8 )
	p.cin = 0xf; // realtime
      if( p.evnt0 == 0xf1 || p.evnt0 == 0xf3 )
	p.cin = 2; // two byte system common message
      else
	p.cin = 3; // three byte system common message
    } else {
      p.cin = p.evnt0 >> 4;
    }

    // propagate to application
    // port is located in method argument
    UIP_TASK_MUTEX_MIDIIN_TAKE;
    if( MIOS32_MIDI_SendPackageToRxCallback(method_arg, p) < 1 )
      APP_MIDI_NotifyPackage(method_arg, p);
    UIP_TASK_MUTEX_MIDIIN_GIVE;

  } else  if( osc_args->arg_type[0] == 'b' ) {
    // SysEx stream is embedded into blob
    u32 len = MIOS32_OSC_GetBlobLength(osc_args->arg_ptr[0]);
    u8 *blob = MIOS32_OSC_GetBlobData(osc_args->arg_ptr[0]);

    // propagate to application
    // port is located in method argument
    int i;
    for(i=0; i<len; ++i, blob++) {
      APP_SYSEX_Parser(method_arg, *blob);
      if( *blob == 0xf7 )
	break;
    }
  } else
    return -2; // wrong argument type for first parameter


  return 0; // no error
}


static s32 OSC_SERVER_Method_MCMPP(mios32_osc_args_t *osc_args, u32 method_arg)
{
  int i;

#if DEBUG_VERBOSE_LEVEL >= 2
  UIP_TASK_MUTEX_MIDIOUT_TAKE;
  MIOS32_OSC_SendDebugMessage(osc_args, method_arg);
  UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

  // we expect at least 1 argument
  if( osc_args->num_args < 1 )
    return -1; // wrong number of arguments

  // extract value and channel from original path
  // format: /mcmpp/name/<value>/<channel>
  // /mcmpp and /name have already been parsed, we only need the values
  char *path_values = (char *)osc_args->original_path;
  for(i=0; i<2; ++i)
    if( (path_values = strchr(path_values+1, '/')) == NULL )
      return -2; // invalid format
  ++path_values;

  int note = 0;
  if( method_arg < 0xc0 ) {
      // (key) value not transmitted for 0xc0 (program change), 0xd0 (aftertouch), 0xe0 (pitch)

    // get value
    note = atoi(path_values);

    // next slash
    if( (path_values = strchr(path_values+1, '/')) == NULL )
      return -2; // invalid format
    ++path_values;
  }

  // get channel
  int chn = atoi(path_values) - 1;
  if( chn < 0 || chn >= 15 )
    return -3; // invalid channel

  // get status nibble and merge with channel
  int evnt0 = (method_arg & 0xf0) | chn;

  // build MIDI package
  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = evnt0 >> 4;
  p.evnt0 = evnt0;

  // pitch bender?
  if( (evnt0 & 0xf0) == 0xe0 ) {
    // get velocity resp. CC value
    int pitch = 8192;
    if( osc_args->arg_type[0] == 'i' )
      pitch = MIOS32_OSC_GetInt(osc_args->arg_ptr[0]);
    else if( osc_args->arg_type[0] == 'f' )
      pitch = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[0]) * 8191.0);
    pitch += 8192;
    if( pitch < 0 ) pitch = 0; else if( pitch > 16383 ) pitch = 16383;
    p.evnt1 = pitch & 0x7f;
    p.evnt2 = (pitch >> 7) & 0x7f;
  } else {
    // get velocity resp. CC value
    int velocity = 127;
    if( osc_args->arg_type[0] == 'i' )
      velocity = MIOS32_OSC_GetInt(osc_args->arg_ptr[0]);
    else if( osc_args->arg_type[0] == 'f' )
      velocity = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[0]) * 127.0);
    if( velocity < 0 ) velocity = 0; else if( velocity > 127 ) velocity = 127;

    p.evnt1 = note;
    p.evnt2 = velocity;
  }

  // propagate to application
  // port is located in method argument

  // search for ports which are assigned to the MCMPP protocol
  u8 transfer_mode = OSC_CLIENT_TransferModeGet(osc_parsed_from_con);
  if( OSC_IGNORE_TRANSFER_MODE || transfer_mode == OSC_CLIENT_TRANSFER_MODE_MCMPP ) {
    UIP_TASK_MUTEX_MIDIIN_TAKE;
    if( MIOS32_MIDI_SendPackageToRxCallback(OSC0 + osc_parsed_from_con, p) < 1 )
      APP_MIDI_NotifyPackage(OSC0 + osc_parsed_from_con, p);
    UIP_TASK_MUTEX_MIDIIN_GIVE;
  }

  return 0; // no error
}


static s32 OSC_SERVER_Method_Event(mios32_osc_args_t *osc_args, u32 method_arg)
{
#if DEBUG_VERBOSE_LEVEL >= 2
  UIP_TASK_MUTEX_MIDIOUT_TAKE;
  MIOS32_OSC_SendDebugMessage(osc_args, method_arg);
  UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

  // we expect at least 1 argument
  if( osc_args->num_args < 1 )
    return -1; // wrong number of arguments

  // get channel and status nibble
  int evnt0 = method_arg & 0xff;

  // get note
  int note = 60;
  if( osc_args->arg_type[0] == 'i' )
    note = MIOS32_OSC_GetInt(osc_args->arg_ptr[0]);
  else if( osc_args->arg_type[0] == 'f' )
    note = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[0]) * 127.0);
  if( note < 0 ) note = 0; else if( note > 127 ) note = 127;

  // get velocity
  int velocity = 127;
  if( osc_args->num_args >= 2 ) {
    if( osc_args->arg_type[1] == 'i' )
      velocity = MIOS32_OSC_GetInt(osc_args->arg_ptr[1]);
    else if( osc_args->arg_type[1] == 'f' )
      velocity = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[1]) * 127.0);
    if( velocity < 0 ) velocity = 0; else if( velocity > 127 ) velocity = 127;
  }

  // build MIDI package
  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = evnt0 >> 4;
  p.evnt0 = evnt0;
  p.evnt1 = note;
  p.evnt2 = velocity;

  // propagate to application
  // search for ports which are assigned to the MIDI value protocol
  u8 transfer_mode = OSC_CLIENT_TransferModeGet(osc_parsed_from_con);
  if( OSC_IGNORE_TRANSFER_MODE ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_INT ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_FLOAT ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_TOSC ) {
    UIP_TASK_MUTEX_MIDIIN_TAKE;
    if( MIOS32_MIDI_SendPackageToRxCallback(OSC0 + osc_parsed_from_con, p) < 1 )
      APP_MIDI_NotifyPackage(OSC0 + osc_parsed_from_con, p);
    UIP_TASK_MUTEX_MIDIIN_GIVE;
  }

  return 0; // no error
}

static s32 OSC_SERVER_Method_EventPB(mios32_osc_args_t *osc_args, u32 method_arg)
{
#if DEBUG_VERBOSE_LEVEL >= 2
  UIP_TASK_MUTEX_MIDIOUT_TAKE;
  MIOS32_OSC_SendDebugMessage(osc_args, method_arg);
  UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

  // we expect at least 1 argument
  if( osc_args->num_args < 1 )
    return -1; // wrong number of arguments

  // get channel and status nibble
  int evnt0 = method_arg & 0xff;

  // get pitchbender value
  int value = 8192;
  if( osc_args->arg_type[0] == 'i' )
    value = MIOS32_OSC_GetInt(osc_args->arg_ptr[0]);
  else if( osc_args->arg_type[0] == 'f' )
    value = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[0]) * 8191.0);
  value += 8192;
  if( value < 0 ) value = 0; else if( value > 16383 ) value = 16383;

  // build MIDI package
  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = evnt0 >> 4;
  p.evnt0 = evnt0;
  p.evnt1 = value & 0x7f;
  p.evnt2 = (value >> 7) & 0x7f;

  // propagate to application
  // search for ports which are assigned to the MIDI value protocol
  u8 transfer_mode = OSC_CLIENT_TransferModeGet(osc_parsed_from_con);
  if( OSC_IGNORE_TRANSFER_MODE ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_INT ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_FLOAT ) {
    UIP_TASK_MUTEX_MIDIIN_TAKE;
    if( MIOS32_MIDI_SendPackageToRxCallback(OSC0 + osc_parsed_from_con, p) < 1 )
      APP_MIDI_NotifyPackage(OSC0 + osc_parsed_from_con, p);
    UIP_TASK_MUTEX_MIDIIN_GIVE;
  }

  return 0; // no error
}

static s32 OSC_SERVER_Method_EventNRPN(mios32_osc_args_t *osc_args, u32 method_arg)
{
#if DEBUG_VERBOSE_LEVEL >= 2
  UIP_TASK_MUTEX_MIDIOUT_TAKE;
  MIOS32_OSC_SendDebugMessage(osc_args, method_arg);
  UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

  // we expect at least 2 arguments
  if( osc_args->num_args < 2 )
    return -1; // wrong number of arguments

  // get channel and status nibble
  int evnt0 = method_arg & 0xff;

  // get NRPN number
  int nrpn_number = 0;
  if( osc_args->arg_type[0] == 'i' )
    nrpn_number = MIOS32_OSC_GetInt(osc_args->arg_ptr[0]);
  else if( osc_args->arg_type[0] == 'f' )
    nrpn_number = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[0]));

  // get NRPN value
  int nrpn_value = 0;
  if( osc_args->arg_type[0] == 'i' )
    nrpn_value = MIOS32_OSC_GetInt(osc_args->arg_ptr[1]);
  else if( osc_args->arg_type[0] == 'f' )
    nrpn_value = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[1]));

  // build MIDI package(s)
  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = evnt0 >> 4;
  p.evnt0 = evnt0;

  // propagate to application
  // search for ports which are assigned to the MIDI value protocol
  u8 transfer_mode = OSC_CLIENT_TransferModeGet(osc_parsed_from_con);
  if( OSC_IGNORE_TRANSFER_MODE ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_INT ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_FLOAT ) {

    // send 4 packages
    UIP_TASK_MUTEX_MIDIIN_TAKE;

    int i;
    for(i=0; i<4; ++i) {
      switch( i ) {
      case 0: // NRPN Address MSB
	p.evnt1 = 0x63;
	p.evnt2 = (nrpn_number >> 7) & 0x7f;
	break;

      case 1: // NRPN Address LSB
	p.evnt1 = 0x62;
	p.evnt2 = (nrpn_number >> 0) & 0x7f;
	break;

      case 2: // NRPN Data MSB
	p.evnt1 = 0x06;
	p.evnt2 = (nrpn_value >> 7) & 0x7f;
	break;

      case 3: // NRPN Data LSB
	p.evnt1 = 0x26;
	p.evnt2 = (nrpn_value >> 0) & 0x7f;
	break;
      }

      if( MIOS32_MIDI_SendPackageToRxCallback(OSC0 + osc_parsed_from_con, p) < 1 )
	APP_MIDI_NotifyPackage(OSC0 + osc_parsed_from_con, p);
    }

    UIP_TASK_MUTEX_MIDIIN_GIVE;
  }

  return 0; // no error
}

static s32 OSC_SERVER_Method_EventTOSC(mios32_osc_args_t *osc_args, u32 method_arg)
{
#if DEBUG_VERBOSE_LEVEL >= 2
  UIP_TASK_MUTEX_MIDIOUT_TAKE;
  MIOS32_OSC_SendDebugMessage(osc_args, method_arg);
  UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

  // get channel and status nibble
  int evnt0 = method_arg & 0xff;

  // check for TouchOSC format (note/CC number coded in path)
  // extract value and channel from original path
  // format: /<chn>/name_<value>
  // /<chn> and /name have already been parsed, we only need the last value
  char *path_values = (char *)osc_args->original_path;
  if( (path_values = strchr(path_values+1, '_')) == NULL )
    return -1;

  // get value
  int note = 60;
  note = atoi(path_values+1);
  if( note < 0 ) note = 0; else if( note > 127 ) note = 127;

  // get velocity
  int velocity = 127;
  if( osc_args->num_args >= 1 ) {
    if( osc_args->arg_type[0] == 'i' )
      velocity = MIOS32_OSC_GetInt(osc_args->arg_ptr[0]);
    else if( osc_args->arg_type[0] == 'f' )
      velocity = (int)(MIOS32_OSC_GetFloat(osc_args->arg_ptr[0]) * 127.0);
  }
  if( velocity < 0 ) velocity = 0; else if( velocity > 127 ) velocity = 127;

  // build MIDI package
  mios32_midi_package_t p;
  p.ALL = 0;
  p.type = evnt0 >> 4;
  p.evnt0 = evnt0;
  p.evnt1 = note;
  p.evnt2 = velocity;

  // propagate to application
  // search for ports which are assigned to the MIDI value protocol
  u8 transfer_mode = OSC_CLIENT_TransferModeGet(osc_parsed_from_con);
  if( OSC_IGNORE_TRANSFER_MODE ||
      transfer_mode == OSC_CLIENT_TRANSFER_MODE_TOSC ) {
    UIP_TASK_MUTEX_MIDIIN_TAKE;
    if( MIOS32_MIDI_SendPackageToRxCallback(OSC0 + osc_parsed_from_con, p) < 1 )
      APP_MIDI_NotifyPackage(OSC0 + osc_parsed_from_con, p);
    UIP_TASK_MUTEX_MIDIIN_GIVE;
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// ESP8266 receive handler
/////////////////////////////////////////////////////////////////////////////
#if OSC_SERVER_ESP8266_ENABLED
static s32 OSC_SERVER_ESP8266_NotifyUdpPacket(u32 ip, u16 port, u8 *payload, u32 len)
{
  u8 udp_monitor_level = UIP_TASK_UDP_MonitorLevelGet();

#if 0
    DEBUG_MSG("> From: %d.%d.%d.%d:%d\n", (ip >> 0) & 0xff, (ip >> 8) & 0xff, (ip >> 16) & 0xff, (ip >> 24) & 0xff, port);
    MIOS32_MIDI_SendDebugHexDump(payload, len);
#endif

  // check for matching port
  int con;

  u8 port_ok = 0;
  for(con=0; con<OSC_SERVER_NUM_CONNECTIONS; ++con) {
    if( osc_local_port[con] == port &&
#if 1
	(osc_remote_ip[con] == 0xffffffff ||  // check for matching IP as well if != 0xffffffff (broadcast IP)
	 ((((osc_remote_ip[con] >> 24) ^ (ip >>  0)) & 0xff) == 0 &&
	  (((osc_remote_ip[con] >> 16) ^ (ip >>  8)) & 0xff) == 0 &&
	  (((osc_remote_ip[con] >>  8) ^ (ip >> 16)) & 0xff) == 0 &&
	  (((osc_remote_ip[con] >>  0) ^ (ip >> 24)) & 0xff) == 0)

	 )
#endif
	) {
      port_ok = 1;
      break;
    }
  }

  if( !port_ok ) {
    // forward to monitor
    if( udp_monitor_level >= UDP_MONITOR_LEVEL_4_ALL ||
	(udp_monitor_level >= UDP_MONITOR_LEVEL_3_ALL_GEQ_1024 && port >= 1024) )
      UIP_TASK_UDP_ESP8266_MonitorPacket(UDP_MONITOR_RECEIVED, "UNMATCHED_PORT", ip, port, payload, len, 0);
  } else {
    // forward to monitor
    if( udp_monitor_level >= UDP_MONITOR_LEVEL_1_OSC_REC )
      UIP_TASK_UDP_ESP8266_MonitorPacket(UDP_MONITOR_RECEIVED, "OSC_RECEIVED", ip, port, payload, len, port);

    // new UDP package has been received
#if DEBUG_VERBOSE_LEVEL >= 3
    UIP_TASK_MUTEX_MIDIOUT_TAKE;
    DEBUG_MSG("[OSC_SERVER] Received Datagram from %d.%d.%d.%d:%d (%d bytes)\n",
	      (ip >> 0) & 0xff,
	      (ip >> 8) & 0xff,
	      (ip >> 16) & 0xff,
	      (ip >> 24) & 0xff,
	      port,
	      len);
    MIOS32_MIDI_SendDebugHexDump((u8 *)payload, len);
    UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif

    osc_parsed_from_con = con; // used by event propagation
    s32 status = MIOS32_OSC_ParsePacket((u8 *)payload, len, parse_root);
    if( status < 0 ) {
#if DEBUG_VERBOSE_LEVEL >= 2
      UIP_TASK_MUTEX_MIDIOUT_TAKE;
      DEBUG_MSG("[OSC_SERVER] invalid OSC packet, status %d\n", status);
      UIP_TASK_MUTEX_MIDIOUT_GIVE;
#endif
    }
  }

  return 0; // no error
}
#endif

/////////////////////////////////////////////////////////////////////////////
// Search Tree for OSC Methods (used by MIOS32_OSC_ParsePacket())
/////////////////////////////////////////////////////////////////////////////

const static mios32_osc_search_tree_t parse_mcmpp_value[] = {
  { "*", NULL, &OSC_SERVER_Method_MCMPP, 0x00000000 },

  { NULL, NULL, NULL, 0 } // terminator
};

const static mios32_osc_search_tree_t parse_mcmpp[] = {
  { "key",           parse_mcmpp_value, NULL, 0x00000090 }, // bit [7:4] contains status byte
  { "polypressure",  parse_mcmpp_value, NULL, 0x000000a0 }, // bit [7:4] contains status byte
  { "cc",            parse_mcmpp_value, NULL, 0x000000b0 }, // bit [7:4] contains status byte
  { "programchange", parse_mcmpp_value, NULL, 0x000000c0 }, // bit [7:4] contains status byte
  { "aftertouch",    parse_mcmpp_value, NULL, 0x000000d0 }, // bit [7:4] contains status byte
  { "pitch",         parse_mcmpp_value, NULL, 0x000000e0 }, // bit [7:4] contains status byte

  { NULL, NULL, NULL, 0 } // terminator
};


const static mios32_osc_search_tree_t parse_event[] = {
  { "note_*",        NULL, &OSC_SERVER_Method_EventTOSC,0x00000090 }, // bit [7:4] contains status byte
  { "polypressure_*",NULL, &OSC_SERVER_Method_EventTOSC,0x000000a0 }, // bit [7:4] contains status byte
  { "cc_*",          NULL, &OSC_SERVER_Method_EventTOSC,0x000000b0 }, // bit [7:4] contains status byte
  { "programchange_*", NULL, &OSC_SERVER_Method_EventTOSC,0x000000c0 }, // bit [7:4] contains status byte

  { "note",          NULL, &OSC_SERVER_Method_Event,   0x00000090 }, // bit [7:4] contains status byte
  { "polypressure",  NULL, &OSC_SERVER_Method_Event,   0x000000a0 }, // bit [7:4] contains status byte
  { "cc",            NULL, &OSC_SERVER_Method_Event,   0x000000b0 }, // bit [7:4] contains status byte
  { "nrpn",          NULL, &OSC_SERVER_Method_EventNRPN,0x000000b0 }, // bit [7:4] contains status byte
  { "programchange", NULL, &OSC_SERVER_Method_Event,   0x000000c0 }, // bit [7:4] contains status byte
  { "aftertouch",    NULL, &OSC_SERVER_Method_Event,   0x000000b0 }, // bit [7:4] contains status byte
  { "pitchbend",     NULL, &OSC_SERVER_Method_EventPB, 0x000000e0 }, // bit [7:4] contains status byte

  { NULL, NULL, NULL, 0 } // terminator
};


const static mios32_osc_search_tree_t parse_root[] = {
  { "midi",  NULL, &OSC_SERVER_Method_MIDI, OSC0 },
  { "midi1", NULL, &OSC_SERVER_Method_MIDI, OSC0 },
  { "midi2", NULL, &OSC_SERVER_Method_MIDI, OSC1 },
  { "midi3", NULL, &OSC_SERVER_Method_MIDI, OSC2 },
  { "midi4", NULL, &OSC_SERVER_Method_MIDI, OSC3 },

  { "mcmpp", parse_mcmpp, NULL, 0x00000000}, // pianist pro format

  { "1",  parse_event, NULL, 0x00000000}, // bit [0:3] selects MIDI channel
  { "2",  parse_event, NULL, 0x00000001}, // bit [0:3] selects MIDI channel
  { "3",  parse_event, NULL, 0x00000002}, // bit [0:3] selects MIDI channel
  { "4",  parse_event, NULL, 0x00000003}, // bit [0:3] selects MIDI channel
  { "5",  parse_event, NULL, 0x00000004}, // bit [0:3] selects MIDI channel
  { "6",  parse_event, NULL, 0x00000005}, // bit [0:3] selects MIDI channel
  { "7",  parse_event, NULL, 0x00000006}, // bit [0:3] selects MIDI channel
  { "8",  parse_event, NULL, 0x00000007}, // bit [0:3] selects MIDI channel
  { "9",  parse_event, NULL, 0x00000008}, // bit [0:3] selects MIDI channel
  { "10", parse_event, NULL, 0x00000009}, // bit [0:3] selects MIDI channel
  { "11", parse_event, NULL, 0x0000000a}, // bit [0:3] selects MIDI channel
  { "12", parse_event, NULL, 0x0000000b}, // bit [0:3] selects MIDI channel
  { "13", parse_event, NULL, 0x0000000c}, // bit [0:3] selects MIDI channel
  { "14", parse_event, NULL, 0x0000000d}, // bit [0:3] selects MIDI channel
  { "15", parse_event, NULL, 0x0000000e}, // bit [0:3] selects MIDI channel
  { "16", parse_event, NULL, 0x0000000f}, // bit [0:3] selects MIDI channel

  { NULL, NULL, NULL, 0 } // terminator
};

#endif
