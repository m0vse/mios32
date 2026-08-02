// $Id$
//! \defgroup SEQ_MIDI_OUT
//!
//! Functions for schedules MIDI output
//!
//! \{
/* ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

/////////////////////////////////////////////////////////////////////////////
// Include files
/////////////////////////////////////////////////////////////////////////////

#include <mios32.h>

#include <stdint.h>

#include "seq_midi_out.h"
#include "seq_bpm.h"

#if SEQ_MIDI_OUT_MALLOC_METHOD != 5
// FreeRTOS based malloc required
#include <FreeRTOS.h>
#if SEQ_MIDI_OUT_MALLOC_PRECHECK
#include <task.h>
#endif
#endif


/////////////////////////////////////////////////////////////////////////////
// for optional debugging messages via MIDI
/////////////////////////////////////////////////////////////////////////////
#define DEBUG_VERBOSE_LEVEL 1
#ifndef DEBUG_MSG
#define DEBUG_MSG MIOS32_MIDI_SendDebugMessage
#endif


/////////////////////////////////////////////////////////////////////////////
// Local types
/////////////////////////////////////////////////////////////////////////////

// an item of the MIDI output queue
typedef struct seq_midi_out_queue_item_t {
  u8                    port;
  u8                    event_type;
  u16                   len;
  mios32_midi_package_t package;
  u32                   timestamp;
#if SEQ_MIDI_OUT_MALLOC_METHOD == 4 || SEQ_MIDI_OUT_MALLOC_METHOD == 5
  struct seq_midi_out_queue_item_t *next;
#endif
} seq_midi_out_queue_item_t;


/////////////////////////////////////////////////////////////////////////////
// Local prototypes
/////////////////////////////////////////////////////////////////////////////

static seq_midi_out_queue_item_t *SEQ_MIDI_OUT_SlotMalloc(void);
static void SEQ_MIDI_OUT_SlotFree(seq_midi_out_queue_item_t *item);
static seq_midi_out_queue_item_t *SEQ_MIDI_OUT_ItemNextGet(const seq_midi_out_queue_item_t *item);
static void SEQ_MIDI_OUT_ItemNextSet(seq_midi_out_queue_item_t *item, seq_midi_out_queue_item_t *next);
#if SEQ_MIDI_OUT_MALLOC_METHOD >= 0 && SEQ_MIDI_OUT_MALLOC_METHOD <= 3
static seq_midi_out_queue_item_t *SEQ_MIDI_OUT_ItemFromIndex(u32 index);
static s32 SEQ_MIDI_OUT_ItemIndex(const seq_midi_out_queue_item_t *item);
static s32 SEQ_MIDI_OUT_PoolGrow(void);
#endif


/////////////////////////////////////////////////////////////////////////////
// Global variables
/////////////////////////////////////////////////////////////////////////////

//! contains number of events which have allocated memory
u32 seq_midi_out_allocated;

//! only for analysis purposes - has to be enabled with SEQ_MIDI_OUT_MALLOC_ANALYSIS
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
u32 seq_midi_out_max_allocated;
u32 seq_midi_out_dropouts;
#endif


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static s32 (*callback_midi_send_package)(mios32_midi_port_t port, mios32_midi_package_t midi_package);
static s32 (*callback_bpm_is_running)(void);
static u32 (*callback_bpm_tick_get)(void);
static s32 (*callback_bpm_set)(float bpm);

static seq_midi_out_queue_item_t *midi_queue;


#if SEQ_MIDI_OUT_MALLOC_METHOD >= 0 && SEQ_MIDI_OUT_MALLOC_METHOD <= 3

#if SEQ_MIDI_OUT_MAX_EVENTS >= 0xffff
# error "Compact scheduler links support fewer than 65535 events"
#endif

#if SEQ_MIDI_OUT_POOL_CHUNK_EVENTS < 1 || SEQ_MIDI_OUT_POOL_CHUNK_EVENTS > SEQ_MIDI_OUT_MAX_EVENTS
# error "SEQ_MIDI_OUT_POOL_CHUNK_EVENTS must be in the range 1..SEQ_MIDI_OUT_MAX_EVENTS"
#endif

#if (SEQ_MIDI_OUT_MAX_EVENTS % SEQ_MIDI_OUT_POOL_CHUNK_EVENTS) != 0
# error "SEQ_MIDI_OUT_POOL_CHUNK_EVENTS must divide SEQ_MIDI_OUT_MAX_EVENTS"
#endif

#define SEQ_MIDI_OUT_POOL_NUM_CHUNKS \
  (SEQ_MIDI_OUT_MAX_EVENTS / SEQ_MIDI_OUT_POOL_CHUNK_EVENTS)

// determine flag array width and mask
#if SEQ_MIDI_OUT_MALLOC_METHOD == 0
# define SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH 1
# define SEQ_MIDI_OUT_MALLOC_FLAG_MASK  1
  static u8 alloc_flags[SEQ_MIDI_OUT_MAX_EVENTS];
#elif SEQ_MIDI_OUT_MALLOC_METHOD == 1
# define SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH 8
# define SEQ_MIDI_OUT_MALLOC_FLAG_MASK  0xff
  static u8 alloc_flags[SEQ_MIDI_OUT_MAX_EVENTS/8];
#elif SEQ_MIDI_OUT_MALLOC_METHOD == 2
# define SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH 16
# define SEQ_MIDI_OUT_MALLOC_FLAG_MASK  0xffff
  static u16 alloc_flags[SEQ_MIDI_OUT_MAX_EVENTS/16];
#else
# define SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH 32
# define SEQ_MIDI_OUT_MALLOC_FLAG_MASK  0xffffffff
  static u32 alloc_flags[SEQ_MIDI_OUT_MAX_EVENTS/32];
#endif

// Each entry points to one allocation containing event payloads followed by
// compact u16 links. Allocated chunks are retained until FreeHeap(), avoiding
// the fragmentation caused by per-event allocation.
static seq_midi_out_queue_item_t *alloc_heap[SEQ_MIDI_OUT_POOL_NUM_CHUNKS];
static u32 alloc_num_chunks;
static u32 alloc_pos;
#endif

static u8 alloc_failure_reported;


/////////////////////////////////////////////////////////////////////////////
// Allows applications to surface a recoverable scheduler allocation failure
// in their own UI. The default implementation reports it on the configured
// MIOS32 debug port.
/////////////////////////////////////////////////////////////////////////////
__attribute__((weak)) void SEQ_MIDI_OUT_NotifyAllocationFailure(u32 requested_bytes,
						 u32 free_bytes,
						 u32 largest_free_block)
{
  DEBUG_MSG("ERROR: MIDI scheduler allocation failed: requested %u bytes, "
	    "%u bytes free, largest block %u bytes\n",
	    requested_bytes, free_bytes, largest_free_block);
}

/////////////////////////////////////////////////////////////////////////////
// Queue links are stored separately for pool allocation methods.  This keeps
// each event payload naturally aligned while reducing it from 16 to 12 bytes
// on 32-bit targets.  Per-item malloc methods retain native pointers.
/////////////////////////////////////////////////////////////////////////////
static seq_midi_out_queue_item_t *SEQ_MIDI_OUT_ItemNextGet(const seq_midi_out_queue_item_t *item)
{
#if SEQ_MIDI_OUT_MALLOC_METHOD == 4 || SEQ_MIDI_OUT_MALLOC_METHOD == 5
  return item->next;
#else
  s32 index = SEQ_MIDI_OUT_ItemIndex(item);
  if( index < 0 )
    return NULL;

  u32 chunk = (u32)index / SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  u32 offset = (u32)index % SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  u16 *links = (u16 *)((u8 *)alloc_heap[chunk] +
			       sizeof(seq_midi_out_queue_item_t) * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS);
  u16 next = links[offset];
  return next == 0xffff ? NULL : SEQ_MIDI_OUT_ItemFromIndex(next);
#endif
}


static void SEQ_MIDI_OUT_ItemNextSet(seq_midi_out_queue_item_t *item,
				     seq_midi_out_queue_item_t *next)
{
#if SEQ_MIDI_OUT_MALLOC_METHOD == 4 || SEQ_MIDI_OUT_MALLOC_METHOD == 5
  item->next = next;
#else
  s32 index = SEQ_MIDI_OUT_ItemIndex(item);
  if( index < 0 )
    return;

  u32 chunk = (u32)index / SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  u32 offset = (u32)index % SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  u16 *links = (u16 *)((u8 *)alloc_heap[chunk] +
			       sizeof(seq_midi_out_queue_item_t) * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS);
  s32 next_index = next == NULL ? -1 : SEQ_MIDI_OUT_ItemIndex(next);
  links[offset] = next_index < 0 ? 0xffff : (u16)next_index;
#endif
}


#if SEQ_MIDI_OUT_MALLOC_METHOD >= 0 && SEQ_MIDI_OUT_MALLOC_METHOD <= 3
/////////////////////////////////////////////////////////////////////////////
// Translate between compact global pool indices and chunk-local pointers.
/////////////////////////////////////////////////////////////////////////////
static seq_midi_out_queue_item_t *SEQ_MIDI_OUT_ItemFromIndex(u32 index)
{
  if( index >= SEQ_MIDI_OUT_MAX_EVENTS )
    return NULL;

  u32 chunk = index / SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  if( chunk >= alloc_num_chunks || alloc_heap[chunk] == NULL )
    return NULL;

  return &alloc_heap[chunk][index % SEQ_MIDI_OUT_POOL_CHUNK_EVENTS];
}


static s32 SEQ_MIDI_OUT_ItemIndex(const seq_midi_out_queue_item_t *item)
{
  uintptr_t address = (uintptr_t)item;
  u32 chunk;

  for(chunk=0; chunk<alloc_num_chunks; ++chunk) {
    uintptr_t begin = (uintptr_t)alloc_heap[chunk];
    uintptr_t end = begin +
      sizeof(seq_midi_out_queue_item_t) * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;

    if( address >= begin && address < end ) {
      uintptr_t byte_offset = address - begin;
      if( (byte_offset % sizeof(seq_midi_out_queue_item_t)) == 0 )
	return (s32)(chunk * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS +
		     byte_offset / sizeof(seq_midi_out_queue_item_t));
      return -1;
    }
  }

  return -1;
}


/////////////////////////////////////////////////////////////////////////////
// Add one retained pool chunk. This deliberately uses a small number of
// coarse allocations rather than per-event malloc/free operations.
/////////////////////////////////////////////////////////////////////////////
static s32 SEQ_MIDI_OUT_PoolGrow(void)
{
  if( alloc_num_chunks >= SEQ_MIDI_OUT_POOL_NUM_CHUNKS )
    return -1;

  const size_t item_bytes =
    sizeof(seq_midi_out_queue_item_t) * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  const size_t link_bytes = sizeof(u16) * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  const size_t requested_bytes = item_bytes + link_bytes;

#if SEQ_MIDI_OUT_MALLOC_PRECHECK
  // FreeRTOS heap implementations account for an aligned two-word block
  // header. Check the largest block while allocation is suspended so a
  // recoverable shortage cannot invoke the application's fatal malloc hook.
  const size_t required_block_size =
    (requested_bytes + (2 * sizeof(size_t)) + portBYTE_ALIGNMENT_MASK) &
    ~(size_t)portBYTE_ALIGNMENT_MASK;
  HeapStats_t heap_stats;

  vTaskSuspendAll();
  vPortGetHeapStats(&heap_stats);
  if( heap_stats.xSizeOfLargestFreeBlockInBytes < required_block_size ) {
    (void)xTaskResumeAll();
    if( !alloc_failure_reported ) {
      alloc_failure_reported = 1;
      SEQ_MIDI_OUT_NotifyAllocationFailure((u32)requested_bytes,
					   (u32)heap_stats.xAvailableHeapSpaceInBytes,
					   (u32)heap_stats.xSizeOfLargestFreeBlockInBytes);
    }
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
    ++seq_midi_out_dropouts;
#endif
    return -1;
  }
#endif

  seq_midi_out_queue_item_t *chunk =
    (seq_midi_out_queue_item_t *)pvPortMalloc(requested_bytes);
#if SEQ_MIDI_OUT_MALLOC_PRECHECK
  (void)xTaskResumeAll();
#endif
  if( chunk == NULL ) {
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
    ++seq_midi_out_dropouts;
#endif
    return -1;
  }

  alloc_heap[alloc_num_chunks++] = chunk;
  return 0;
}
#endif

#if SEQ_MIDI_OUT_SUPPORT_DELAY
#if SEQ_MIDI_OUT_PPQN_DELAY_NUM < 1 || SEQ_MIDI_OUT_PPQN_DELAY_NUM > 256
# error "SEQ_MIDI_OUT_PPQN_DELAY_NUM must be in the range 1..256"
#endif
static s8 ppqn_delay[SEQ_MIDI_OUT_PPQN_DELAY_NUM];
#endif


/////////////////////////////////////////////////////////////////////////////
//! Initialisation of MIDI output scheduler
//! \param[in] mode currently only mode 0 supported
//! \return < 0 if initialisation failed
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Init(u32 mode)
{
  // install default callback functions (selected with NULL)
  SEQ_MIDI_OUT_Callback_MIDI_SendPackage_Set(NULL);
  SEQ_MIDI_OUT_Callback_BPM_IsRunning_Set(NULL);
  SEQ_MIDI_OUT_Callback_BPM_TickGet_Set(NULL);
  SEQ_MIDI_OUT_Callback_BPM_Set_Set(NULL);

  // don't re-initialize queue to ensure that memory can be delocated properly
  // when this function is called multiple times
  // we assume, that gcc will always fill the memory range with zero on application start
  //  midi_queue = NULL;

  seq_midi_out_allocated = 0;
  alloc_failure_reported = 0;
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
  seq_midi_out_max_allocated = 0;
  seq_midi_out_dropouts = 0;
#endif

  // memory will be allocated with first event
  SEQ_MIDI_OUT_FreeHeap();

#if SEQ_MIDI_OUT_SUPPORT_DELAY
  {
    int i;
    for(i=0; i<SEQ_MIDI_OUT_PPQN_DELAY_NUM; ++i) {
      ppqn_delay[i] = 0;
    }
  }
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! Allows to change the function which is called whenever a MIDI package
//! should be sent.
//!
//! This becomes useful if the MIDI output should be filtered, converted
//! or rendered into a MIDI file.
//!
//! \param[in] *_callback_midi_send_package pointer to callback function:<BR>
//! \code
//!   s32 callback_midi_send_package(mios32_midi_port_t port, mios32_midi_package_t midi_package)
//!   {
//!     // ...
//!     // do something with port and midi_package
//!     // ...
//!
//!     return 0; // no error
//!   }
//! \endcode
//! If set to NULL, the default function MIOS32_MIDI_SendPackage function will
//! be used. This allows you to restore the default setup properly.
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Callback_MIDI_SendPackage_Set(void *_callback_midi_send_package)
{
  callback_midi_send_package = (_callback_midi_send_package == NULL)
    ? MIOS32_MIDI_SendPackage
    : _callback_midi_send_package;

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
//! Allows to change the function which is called to check if the BPM generator
//! is running.
//!
//! Together with the other callback functions, this becomes useful if the MIDI
//! output should be rendered into a MIDI file (in this case, the function
//! should return 1, so that SEQ_MIDI_OUT_Handler handler will generate MIDI output)
//!
//! \param[in] *_callback_bpm_is_running pointer to callback function:<BR>
//! \code
//!   s32 callback_bpm_is_running(void)
//!   {
//!     return 1; // always running
//!   }
//! \endcode
//! If set to NULL, the default function SEQ_BPM_IsRunning function will
//! be used. This allows you to restore the default setup properly.
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Callback_BPM_IsRunning_Set(void *_callback_bpm_is_running)
{
  callback_bpm_is_running = (_callback_bpm_is_running == NULL)
    ? SEQ_BPM_IsRunning
    : _callback_bpm_is_running;

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
//! Allows to change the function which is called to retrieve the current
//! BPM tick.
//!
//! Together with the other callback functions, this becomes useful if the MIDI
//! output should be rendered into a MIDI file (in this case, the function
//! should return a number which is incremented after each rendering step).
//!
//! \param[in] *_callback_bpm_tick_get pointer to callback function:<BR>
//! \code
//!   u32 callback_bpm_tick_get(void)
//!   {
//!     return my_bpm_tick; // this variable will be incremented after each rendering step
//!   }
//! \endcode
//! If set to NULL, the default function SEQ_BPM_TickGet function will
//! be used. This allows you to restore the default setup properly.
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Callback_BPM_TickGet_Set(void *_callback_bpm_tick_get)
{
  callback_bpm_tick_get = (_callback_bpm_tick_get == NULL)
    ? SEQ_BPM_TickGet
    : _callback_bpm_tick_get;

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
//! Allows to change the function which is called to change the song tempo.
//!
//! Together with the other callback functions, this becomes useful if the MIDI
//! output should be rendered into a MIDI file (in this case, the function
//! should add the appr. meta event into the MIDI file).
//!
//! \param[in] *_callback_bpm_set pointer to callback function:<BR>
//! \code
//!   u32 callback_bpm_set(float bpm)
//!   {
//!     // ...
//!     // do something with bpm value
//!     // ...
//!
//!     return 0; // no error
//!   }
//! \endcode
//! If set to NULL, the default function SEQ_BPM_Set function will
//! be used. This allows you to restore the default setup properly.
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Callback_BPM_Set_Set(void *_callback_bpm_set)
{
  callback_bpm_set = (_callback_bpm_set == NULL)
    ? SEQ_BPM_Set
    : _callback_bpm_set;

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
//! This function schedules a MIDI event, which will be sent over a given
//! port at a given bpm_tick
//! \param[in] port MIDI port (DEFAULT, USB0..USB7, UART0..UART1, IIC0..IIC7)
//! \param[in] midi_package MIDI package
//! If the re-schedule feature SEQ_MIDI_OUT_ReSchedule() should be used, the
//! mios32_midi_package_t.cable field should be initialized with a tag (supported
//! range: 0-15)
//! \param[in] event_type the event type
//! \param[in] timestamp the bpm_tick value at which the event should be sent
//! \return 0 if event has been scheduled successfully
//! \return -1 if out of memory
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Send(mios32_midi_port_t port, mios32_midi_package_t midi_package, seq_midi_out_event_type_t event_type, u32 timestamp, u32 len)
{
  // failsave measure:
  // don't take On or OnOff item if heap is almost completely allocated
  if( seq_midi_out_allocated >= (SEQ_MIDI_OUT_MAX_EVENTS-2) && // should be enough for a On *and* Off event
      (event_type == SEQ_MIDI_OUT_OnEvent || event_type == SEQ_MIDI_OUT_OnOffEvent) ) {
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
    ++seq_midi_out_dropouts;
#endif
    return -1; // allocation error
  };


#if SEQ_MIDI_OUT_SUPPORT_DELAY
  if( port < SEQ_MIDI_OUT_PPQN_DELAY_NUM ) {
    s8 delay = ppqn_delay[port];
    if( (delay < 0) && (timestamp < -delay) ) {
      timestamp = 0;
    } else {
      timestamp += delay;
    }
  }
#endif

  // create new item
  seq_midi_out_queue_item_t *new_item;
  if( (new_item=SEQ_MIDI_OUT_SlotMalloc()) == NULL ) {
    return -1; // allocation error
  } else {
    new_item->port = port;
    new_item->package = midi_package;
    new_item->event_type = event_type;
    new_item->timestamp = timestamp;
    new_item->len = len;
    SEQ_MIDI_OUT_ItemNextSet(new_item, NULL);
  }

#if DEBUG_VERBOSE_LEVEL >= 2
#if DEBUG_VERBOSE_LEVEL == 2
  if( event_type != SEQ_MIDI_OUT_ClkEvent )
#endif
  DEBUG_MSG("[SEQ_MIDI_OUT_Send:%u] (tag %d) %02x %02x %02x len:%u @%u\n", timestamp, midi_package.cable, midi_package.evnt0, midi_package.evnt1, midi_package.evnt2, len, SEQ_BPM_TickGet());
#endif

  // search in queue for last item which has the same (or earlier) timestamp
  seq_midi_out_queue_item_t *item;
  if( (item=midi_queue) == NULL ) {
    // no item in queue -- first element
    midi_queue = new_item;
  } else {
    u8 insert_before_item = 0;
    seq_midi_out_queue_item_t *last_item = NULL;
    seq_midi_out_queue_item_t *next_item;
    do {
      // Clock and Tempo events are sorted before CC and Note events at a given timestamp
      if( (event_type == SEQ_MIDI_OUT_ClkEvent || event_type == SEQ_MIDI_OUT_TempoEvent ) && 
	  item->timestamp >= timestamp &&
	  (item->event_type == SEQ_MIDI_OUT_OnEvent || 
	   item->event_type == SEQ_MIDI_OUT_OffEvent || 
	   item->event_type == SEQ_MIDI_OUT_OnOffEvent || 
	   item->event_type == SEQ_MIDI_OUT_CCEvent) ) {
	// found any event with same timestamp, insert clock before these events
	// note that the Clock event order doesn't get lost if clock events 
	// are queued at the same timestamp (e.g. MIDI start -> MIDI clock)
	insert_before_item = 1;
	break;
      }

      // CCs are sorted before notes at a given timestamp
      // (new CC before On events at the same timestamp)
      // CCs are still played after Off or Clock events
      if( event_type == SEQ_MIDI_OUT_CCEvent && 
	  item->timestamp == timestamp &&
	  (item->event_type == SEQ_MIDI_OUT_OnEvent || item->event_type == SEQ_MIDI_OUT_OnOffEvent) ) {
	// found On event with same timestamp, play CC before On event
	insert_before_item = 1;
	break;
      }

      if( item->timestamp > timestamp ) {
	// found entry with later timestamp
	insert_before_item = 1;
	break;
      }

      if( (next_item=SEQ_MIDI_OUT_ItemNextGet(item)) == NULL ) {
	// end of queue reached, insert new item at the end
	break;
      }
	
      if( next_item->timestamp > timestamp ) {
	// found entry with later timestamp
	break;
      }

      // switch to next item
      last_item = item;
      item = next_item;
    } while( 1 );

    // insert/add item into/to list
    if( insert_before_item ) {
      if( last_item == NULL )
	midi_queue = new_item;
      else
	SEQ_MIDI_OUT_ItemNextSet(last_item, new_item);
      SEQ_MIDI_OUT_ItemNextSet(new_item, item);
    } else {
      SEQ_MIDI_OUT_ItemNextSet(item, new_item);
      SEQ_MIDI_OUT_ItemNextSet(new_item, next_item);
    }
  }

  // schedule off event now if length > 16bit (since it cannot be stored in event record)
  if( event_type == SEQ_MIDI_OUT_OnOffEvent && len > 0xffff ) {
    return SEQ_MIDI_OUT_Send(port, midi_package, event_type, timestamp+len, 0);
  }

  // display queue
#if DEBUG_VERBOSE_LEVEL >= 4
  DEBUG_MSG("--- vvv ---\n");
  item=midi_queue;
  while( item != NULL ) {
    DEBUG_MSG("[%u] (tag %d) %02x %02x %02x len:%u @%u\n", item->timestamp, item->package.cable, item->package.evnt0, item->package.evnt1, item->package.evnt2, item->len, SEQ_BPM_TickGet());
    item = SEQ_MIDI_OUT_ItemNextGet(item);
  }
  DEBUG_MSG("--- ^^^ ---\n");
  
#endif


  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! This function re-schedules MIDI Off/OnOff events assigned to a given "tag"
//! (0..15, stored in mios32_midi_package_t.cable of events which already have been
//! sent.
//!
//! Usually only SEQ_MIDI_OUT_Off events will be re-scheduled, all events
//! which don't match the event_type will be ignored so that no special tag is required
//! for such events.
//!
//! Usecase: sustained notes can be realized this way: schedule a Note Off
//! event at timestamp 0xffffffff, reschedule it to timestamp == bpm_tick
//! once the sequencer determined, that the off event should be played.
//!
//! \param[in] tag (0..15) the mios32_midi_package.t.cable number of events which should be re-scheduled
//! \param[in] event_type the event type which should be rescheduled
//! \param[in] timestamp the bpm_tick value at which the event should be sent
//! \param[in] reschedule_filter if != NULL, we expect a 4*32 bit word which contains flags for all
//!            Note and CC values which shouldn't be rescheduled (e.g. don't send note off for notes
//!            which are played on a keyboard)
//! 
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_ReSchedule(u8 tag, seq_midi_out_event_type_t event_type, u32 timestamp, u32 *reschedule_filter)
{
  // search in queue for items with the given tag

  seq_midi_out_queue_item_t *prev_item = NULL;
  seq_midi_out_queue_item_t *item = midi_queue;
  while( item != NULL ) {
    // filter event_type and tag
    // and ignore events, which will be played with next invocation of the Out Handler to avoid,
    // that a re-scheduled event will be checked again
    u8 evnt1 = item->package.evnt1;
    if( (item->event_type == event_type) && (item->package.cable == tag) &&
	(reschedule_filter == NULL ||
	 !(reschedule_filter[evnt1>>5] & (1 << (evnt1 & 0x1f)))) ) {
      // ensure that we get a free memory slot by releasing the current item before queuing the off item
#if 0
      seq_midi_out_queue_item_t copy = *item;
#else
      // ???
      seq_midi_out_queue_item_t copy;
      copy.port = item->port;
      copy.event_type = item->event_type;
      copy.len = item->len;
      copy.package.ALL = item->package.ALL;
      copy.timestamp = item->timestamp;
#endif

      u32 delayed_timestamp = timestamp;
#if SEQ_MIDI_OUT_SUPPORT_DELAY
      if( copy.port < SEQ_MIDI_OUT_PPQN_DELAY_NUM ) {
	s8 delay = ppqn_delay[copy.port];
	if( (delay < 0) && (delayed_timestamp < -delay) ) {
	  delayed_timestamp = 0;
	} else {
	  delayed_timestamp += delay;
	}
      }
#endif
      if( item->timestamp <= delayed_timestamp )
	break;

      // remove item from queue
      seq_midi_out_queue_item_t *next_item = SEQ_MIDI_OUT_ItemNextGet(item);
      SEQ_MIDI_OUT_SlotFree(item);
      item = next_item;

      // fix link to next item
      if( prev_item == NULL ) {
	midi_queue = item;
      } else {
	SEQ_MIDI_OUT_ItemNextSet(prev_item, item);
      }

#if DEBUG_VERBOSE_LEVEL >= 2
      DEBUG_MSG("[SEQ_MIDI_OUT_ReSchedule:%u] (tag %d) %02x %02x %02x @%u\n", timestamp, copy.package.cable, copy.package.evnt0, copy.package.evnt1, copy.package.evnt2, SEQ_BPM_TickGet());
#endif

      // re-schedule copied item at new timestamp
      SEQ_MIDI_OUT_Send(copy.port, copy.package, copy.event_type, timestamp, copy.len);

      // determine new prev_item if required
      // TODO: find more elegant solution which doesn't require to search through the linked list!
      if( item != NULL ) {
	prev_item = NULL;
	seq_midi_out_queue_item_t *tmp_item = midi_queue;
	while( tmp_item != NULL ) {
	  if( SEQ_MIDI_OUT_ItemNextGet(tmp_item) == item ) {
	    prev_item = tmp_item;
	    break;
	  } else
	    tmp_item = SEQ_MIDI_OUT_ItemNextGet(tmp_item);
	}

	if( prev_item == NULL ) {
#if DEBUG_VERBOSE_LEVEL >= 1
	  // (always print out - this condition should never happen!)
	  DEBUG_MSG("[SEQ_MIDI_OUT_ReSchedule:%u] Malfunction - prev_item not found anymore!\n", timestamp);
#endif
	  return -1; // data corruption!
	}
      }

    } else {
      // switch to next item
      prev_item = item;
      item = SEQ_MIDI_OUT_ItemNextGet(item);
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! This function empties the queue and plays all "off" events
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_FlushQueue(void)
{
  seq_midi_out_queue_item_t *item;
  while( (item=midi_queue) != NULL ) {
    if( item->event_type == SEQ_MIDI_OUT_OffEvent || item->event_type == SEQ_MIDI_OUT_OnOffEvent ) {
      item->package.velocity = 0; // ensure that velocity is 0
      callback_midi_send_package(item->port, item->package);
    }

    midi_queue = SEQ_MIDI_OUT_ItemNextGet(item);
    SEQ_MIDI_OUT_SlotFree(item);
  }

  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
//! This function frees the complete allocated memory.<BR>
//! It should only be called after SEQ_MIDI_OUT_FlushQueue to prevent stucking
//! Note events
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_FreeHeap(void)
{
  // ensure that all items are delocated
  seq_midi_out_queue_item_t *item;
  while( (item=midi_queue) != NULL ) {
    midi_queue = SEQ_MIDI_OUT_ItemNextGet(item);
    SEQ_MIDI_OUT_SlotFree(item);
  }

  // free memory
#if SEQ_MIDI_OUT_MALLOC_METHOD == 4
  // not relevant
#elif SEQ_MIDI_OUT_MALLOC_METHOD == 5
  // not relevant
#else
  u32 chunk;
  for(chunk=0; chunk<alloc_num_chunks; ++chunk) {
    if( alloc_heap[chunk] != NULL ) {
      vPortFree(alloc_heap[chunk]);
      alloc_heap[chunk] = NULL;
    }
  }

  alloc_num_chunks = 0;
  alloc_pos = 0;
  alloc_failure_reported = 0;
  seq_midi_out_allocated = 0;

  int i;
  for(i=0; i<(SEQ_MIDI_OUT_MAX_EVENTS/SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH); ++i)
    alloc_flags[i] = 0;
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
//! This function should be called periodically (1 mS) to check for timestamped
//! MIDI events which have to be sent.
//! 
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_Handler(void)
{
  // exit if BPM generator not running
  if( !callback_bpm_is_running() )
    return 0;

  // search in queue for items which have to be played now (or have been missed earlier)
  // note that we are going through a sorted list, therefore we can exit once a timestamp
  // has been found which has to be played later than now

  seq_midi_out_queue_item_t *item;
  while( (item=midi_queue) != NULL && item->timestamp <= callback_bpm_tick_get() ) {
#if DEBUG_VERBOSE_LEVEL >= 2
#if DEBUG_VERBOSE_LEVEL == 2
    if( item->event_type != SEQ_MIDI_OUT_ClkEvent )
#endif
    DEBUG_MSG("[SEQ_MIDI_OUT_Handler:%u] (tag %d) %02x %02x %02x @%u\n", item->timestamp, item->package.cable, item->package.evnt0, item->package.evnt1, item->package.evnt2, SEQ_BPM_TickGet());
#endif

    // if tempo event: change BPM stored in midi_package.ALL
    if( item->event_type == SEQ_MIDI_OUT_TempoEvent ) {
      callback_bpm_set(item->package.ALL);
    } else {
      callback_midi_send_package(item->port, item->package);
    }

    // schedule Off event if requested
    if( item->event_type == SEQ_MIDI_OUT_OnOffEvent && item->len ) {
      // ensure that we get a free memory slot by releasing the current item before queuing the off item
#if 0
      seq_midi_out_queue_item_t copy = *item;
#else
      // ???
      seq_midi_out_queue_item_t copy;
      copy.port = item->port;
      copy.event_type = item->event_type;
      copy.len = item->len;
      copy.package.ALL = item->package.ALL;
      copy.timestamp = item->timestamp;
#endif
      copy.package.velocity = 0; // ensure that velocity is 0

      // remove item from queue
      midi_queue = SEQ_MIDI_OUT_ItemNextGet(item);
      SEQ_MIDI_OUT_SlotFree(item);

      u32 delayed_timestamp = copy.len + copy.timestamp;
#if SEQ_MIDI_OUT_SUPPORT_DELAY
      // revert timestamp delay (will be added again by SEQ_MIDI_OUT_Send())
      if( copy.port < SEQ_MIDI_OUT_PPQN_DELAY_NUM ) {
	s8 delay = ppqn_delay[copy.port];
	if( (delay > 0) && (delayed_timestamp < delay) ) {
	  delayed_timestamp = 0;
	} else {
	  delayed_timestamp -= delay;
	}
      }
#endif

      SEQ_MIDI_OUT_Send(copy.port, copy.package, SEQ_MIDI_OUT_OffEvent, delayed_timestamp, 0);
    } else {
      // remove item from queue
      midi_queue = SEQ_MIDI_OUT_ItemNextGet(item);
      SEQ_MIDI_OUT_SlotFree(item);
    }
  }

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// Local function to allocate memory
// returns NULL if no memory free
/////////////////////////////////////////////////////////////////////////////
static seq_midi_out_queue_item_t *SEQ_MIDI_OUT_SlotMalloc(void)
{
  // limit max number of allocted items for all methods
  if( seq_midi_out_allocated >= SEQ_MIDI_OUT_MAX_EVENTS ) {
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
    ++seq_midi_out_dropouts;
#endif
    return NULL;
  }

#if SEQ_MIDI_OUT_MALLOC_METHOD == 4 || SEQ_MIDI_OUT_MALLOC_METHOD == 5
  seq_midi_out_queue_item_t *item;
#if SEQ_MIDI_OUT_MALLOC_METHOD == 4
  if( (item=(seq_midi_out_queue_item_t *)pvPortMalloc(sizeof(seq_midi_out_queue_item_t))) == NULL ) {
#else
  if( (item=(seq_midi_out_queue_item_t *)malloc(sizeof(seq_midi_out_queue_item_t))) == NULL ) {
#endif

#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
    ++seq_midi_out_dropouts;
#endif
    return NULL;
  }

  ++seq_midi_out_allocated;
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
  if( seq_midi_out_allocated > seq_midi_out_max_allocated )
    seq_midi_out_max_allocated = seq_midi_out_allocated;
#endif

  return item;
#else

  ///////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////

  // Start with one coarse retained block. Grow only after every slot in the
  // currently allocated capacity is in use.
  if( alloc_num_chunks == 0 && SEQ_MIDI_OUT_PoolGrow() < 0 )
    return NULL;

  u32 capacity = alloc_num_chunks * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  if( seq_midi_out_allocated >= capacity ) {
    if( SEQ_MIDI_OUT_PoolGrow() < 0 )
      return NULL;
    capacity = alloc_num_chunks * SEQ_MIDI_OUT_POOL_CHUNK_EVENTS;
  }

  // Search only allocated chunks. This simple circular scan also fixes the
  // old packed-flag search, whose starting word accidentally divided by the
  // total event count instead of the flag width.
  s32 new_pos = -1;
  u32 i;
  u32 ix = (alloc_pos + 1) % capacity;
  for(i=0; i<capacity; ++i) {
#if SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH == 1
    if( !alloc_flags[ix] ) {
      alloc_flags[ix] = 1;
      new_pos = (s32)ix;
      break;
    }
#else
    u32 word = ix / SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH;
    u32 mask = (u32)1 << (ix % SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH);
    if( !(alloc_flags[word] & mask) ) {
      alloc_flags[word] |= mask;
      new_pos = (s32)ix;
      break;
    }
#endif
    ix = (ix + 1) % capacity;
  }

  if( new_pos == -1 ) {
    // should never happen! (can be checked by setting a breakpoint or printf to this location)
#if DEBUG_VERBOSE_LEVEL >= 1
      DEBUG_MSG("[SEQ_MIDI_OUT_SlotMalloc] Malfunction case #2\n");
#endif
#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
    ++seq_midi_out_dropouts;
#endif
    return NULL;
  }

  alloc_pos = new_pos;
  ++seq_midi_out_allocated;

#if SEQ_MIDI_OUT_MALLOC_ANALYSIS
  if( seq_midi_out_allocated > seq_midi_out_max_allocated )
    seq_midi_out_max_allocated = seq_midi_out_allocated;
#endif

  return SEQ_MIDI_OUT_ItemFromIndex(alloc_pos);
#endif
}


/////////////////////////////////////////////////////////////////////////////
// Local function to free memory
/////////////////////////////////////////////////////////////////////////////
static void SEQ_MIDI_OUT_SlotFree(seq_midi_out_queue_item_t *item)
{
#if SEQ_MIDI_OUT_MALLOC_METHOD == 4
  vPortFree(item);
  --seq_midi_out_allocated;
#elif SEQ_MIDI_OUT_MALLOC_METHOD == 5
  free(item);
  --seq_midi_out_allocated;
#else

  ///////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////
  ///////////////////////////////////////////////////////////////////////////

  s32 item_index = SEQ_MIDI_OUT_ItemIndex(item);
  if( item_index >= 0 ) {
    u32 pos = (u32)item_index;
#if SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH == 1
    alloc_flags[pos] = 0;
#else
    alloc_flags[pos/SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH] &=
      ~((u32)1 << (pos%SEQ_MIDI_OUT_MALLOC_FLAG_WIDTH));
#endif
    if( seq_midi_out_allocated ) // TODO: check why it can happen that out_allocated == 0
      --seq_midi_out_allocated;
  } else {
    // should never happen! (can be checked by setting a breakpoint or printf to this location)
#if DEBUG_VERBOSE_LEVEL >= 1
    DEBUG_MSG("[SEQ_MIDI_OUT_SlotFree] Malfunction case #1\n");
#endif
    return;
  }
#endif
}

#if SEQ_MIDI_OUT_SUPPORT_DELAY
/////////////////////////////////////////////////////////////////////////////
//! Sets a delay for the given MIDI port
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s32 SEQ_MIDI_OUT_DelaySet(mios32_midi_port_t port, s8 delay)
{
  if( port >= SEQ_MIDI_OUT_PPQN_DELAY_NUM )
    return -1; // invalid port
  ppqn_delay[port] = delay;
  return 0; // no error
}

/////////////////////////////////////////////////////////////////////////////
//! Returns the delay for the given MIDI port
//! \return < 0 on errors
/////////////////////////////////////////////////////////////////////////////
s8  SEQ_MIDI_OUT_DelayGet(mios32_midi_port_t port)
{
  if( port >= SEQ_MIDI_OUT_PPQN_DELAY_NUM )
    return 0;
  return ppqn_delay[port];
}
#endif


//! \}
