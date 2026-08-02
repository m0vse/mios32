// $Id$
/*
 * Header file for tasks which have to be serviced by FreeRTOS/MacOS
 *
 * For MIOS32, the appr. tasks.c file is located in ../mios32/tasks.c
 * For MacOS, the code is implemented in ui.m
 *
 * ==========================================================================
 *
 *  Copyright (C) 2008 Thorsten Klose (tk@midibox.org)
 *  Licensed for personal non-commercial use only.
 *  All other rights reserved.
 * 
 * ==========================================================================
 */

#ifndef _TASKS_H
#define _TASKS_H



#ifndef MIOS32_FAMILY_EMULATION
# include <FreeRTOS.h>
# include <portmacro.h>
# include <task.h>
# include <queue.h>
# include <semphr.h>
#endif


/////////////////////////////////////////////////////////////////////////////
// Global definitions
/////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
extern "C" {
#endif

// this mutex should be used by all tasks which are accessing the SD Card
#ifdef MIOS32_FAMILY_EMULATION
  extern void TASKS_SDCardSemaphoreTake(void);
  extern void TASKS_SDCardSemaphoreGive(void);
# define MUTEX_SDCARD_TAKE { TASKS_SDCardSemaphoreTake(); }
# define MUTEX_SDCARD_GIVE { TASKS_SDCardSemaphoreGive(); }
#else
  extern SemaphoreHandle_t xSDCardSemaphore;
# define MUTEX_SDCARD_TAKE { if( xSDCardSemaphore ) while( xSemaphoreTakeRecursive(xSDCardSemaphore, portMAX_DELAY) != pdTRUE ); }
# define MUTEX_SDCARD_GIVE { if( xSDCardSemaphore ) xSemaphoreGiveRecursive(xSDCardSemaphore); }
#endif


// this mutex should be used by all tasks which access J16 (ENC28J60 and SD Card)
#ifdef MIOS32_FAMILY_EMULATION
# define MUTEX_J16_TAKE { }
# define MUTEX_J16_GIVE { }
#else
  extern SemaphoreHandle_t xJ16Semaphore;
# define MUTEX_J16_TAKE { if( xJ16Semaphore ) while( xSemaphoreTakeRecursive(xJ16Semaphore, portMAX_DELAY) != pdTRUE ); }
# define MUTEX_J16_GIVE { if( xJ16Semaphore ) xSemaphoreGiveRecursive(xJ16Semaphore); }
#endif


// MIDI IN handler
#ifdef MIOS32_FAMILY_EMULATION
  extern void TASKS_MIDIINSemaphoreTake(void);
  extern void TASKS_MIDIINSemaphoreGive(void);
# define MUTEX_MIDIIN_TAKE { TASKS_MIDIINSemaphoreTake(); }
# define MUTEX_MIDIIN_GIVE { TASKS_MIDIINSemaphoreGive(); }
#else
  extern SemaphoreHandle_t xMIDIINSemaphore;
# define MUTEX_MIDIIN_TAKE { if( xMIDIINSemaphore ) while( xSemaphoreTakeRecursive(xMIDIINSemaphore, portMAX_DELAY) != pdTRUE ); }
# define MUTEX_MIDIIN_GIVE { if( xMIDIINSemaphore ) xSemaphoreGiveRecursive(xMIDIINSemaphore); }
#endif


// MIDI OUT handler
#ifdef MIOS32_FAMILY_EMULATION
  extern void TASKS_MIDIOUTSemaphoreTake(void);
  extern void TASKS_MIDIOUTSemaphoreGive(void);
# define MUTEX_MIDIOUT_TAKE { TASKS_MIDIOUTSemaphoreTake(); }
# define MUTEX_MIDIOUT_GIVE { TASKS_MIDIOUTSemaphoreGive(); }
#else
  extern SemaphoreHandle_t xMIDIOUTSemaphore;
# define MUTEX_MIDIOUT_TAKE { if( xMIDIOUTSemaphore ) while( xSemaphoreTakeRecursive(xMIDIOUTSemaphore, portMAX_DELAY) != pdTRUE ); }
# define MUTEX_MIDIOUT_GIVE { if( xMIDIOUTSemaphore ) xSemaphoreGiveRecursive(xMIDIOUTSemaphore); }
#endif


// LCD access
#ifdef MIOS32_FAMILY_EMULATION
  extern void TASKS_LCDSemaphoreTake(void);
  extern void TASKS_LCDSemaphoreGive(void);
# define MUTEX_LCD_TAKE { TASKS_LCDSemaphoreTake(); }
# define MUTEX_LCD_GIVE { TASKS_LCDSemaphoreGive(); }
#else
  extern SemaphoreHandle_t xLCDSemaphore;
# define MUTEX_LCD_TAKE { if( xLCDSemaphore ) while( xSemaphoreTakeRecursive(xLCDSemaphore, portMAX_DELAY) != pdTRUE ); }
# define MUTEX_LCD_GIVE { if( xLCDSemaphore ) xSemaphoreGiveRecursive(xLCDSemaphore); }
#endif

/////////////////////////////////////////////////////////////////////////////
// Global Types
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Prototypes
/////////////////////////////////////////////////////////////////////////////

// called from tasks.c
extern s32 TASKS_Init(u32 mode);
extern u32 TASKS_ResetSourceGet(void);

#ifndef MIOS32_FAMILY_EMULATION
extern s32 TASKS_TarBackupStart(void);
#endif

#define TASKS_WATCHDOG_HEARTBEAT_HOOKS       0
#define TASKS_WATCHDOG_HEARTBEAT_MIDI_HOOKS  1
#define TASKS_WATCHDOG_HEARTBEAT_MIDI        2
#define TASKS_WATCHDOG_HEARTBEAT_LOW_PRIO    3
#define TASKS_WATCHDOG_LONG_OPERATION_MS      600000

extern void TASKS_WatchdogHeartbeat(u8 source);
extern void TASKS_WatchdogSuspend(u32 maximum_ms);
extern void TASKS_WatchdogResume(void);
extern s32 TASKS_WatchdogEnabled(void);
extern u32 TASKS_WatchdogMissingGet(void);
extern u32 TASKS_WatchdogSuspendRemainingGet(void);

extern void SEQ_TASK_MIDI(void);
extern void SEQ_TASK_Period1mS(void);
extern void SEQ_TASK_Period1mS_LowPrio(void);
extern void SEQ_TASK_Period1S(void);
extern void SEQ_TASK_TarBackup(void);


/////////////////////////////////////////////////////////////////////////////
// Export global variables
/////////////////////////////////////////////////////////////////////////////

#ifdef __cplusplus
}
#endif

#endif /* _TASKS_H */
