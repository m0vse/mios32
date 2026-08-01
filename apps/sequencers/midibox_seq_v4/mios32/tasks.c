// $Id$
/*
 * FreeRTOS Tasks
 * only used by MIOS32 build, as a Cocoa based Task handling is used on MacOS
 *
 * ==========================================================================
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

#include "lwip_task.h"

#include "tasks.h"


/////////////////////////////////////////////////////////////////////////////
// Global variables
/////////////////////////////////////////////////////////////////////////////

// for mutual exclusive SD Card access between different tasks
// The mutex is handled with MUTEX_SDCARD_TAKE and MUTEX_SDCARD_GIVE
// macros inside the application, which contain a different implementation 
// for emulation
SemaphoreHandle_t xSDCardSemaphore;

// Mutex for MIDI IN/OUT handler
SemaphoreHandle_t xMIDIINSemaphore;
SemaphoreHandle_t xMIDIOUTSemaphore;

// Mutex for LCD access
SemaphoreHandle_t xLCDSemaphore;

// Mutex for J16 access (SDCard/Ethernet)
SemaphoreHandle_t xJ16Semaphore;


/////////////////////////////////////////////////////////////////////////////
// Local types
/////////////////////////////////////////////////////////////////////////////


/////////////////////////////////////////////////////////////////////////////
// Local definitions
/////////////////////////////////////////////////////////////////////////////

#define PRIORITY_TASK_MIDI		 ( tskIDLE_PRIORITY + 4 )
#define PRIORITY_TASK_PERIOD1MS		 ( tskIDLE_PRIORITY + 2 )
#define PRIORITY_TASK_PERIOD1MS_LOW_PRIO ( tskIDLE_PRIORITY + 2 )

// priority of lwIP task defined in lwip_task.c (-> using 3)


/////////////////////////////////////////////////////////////////////////////
// Local Prototypes
/////////////////////////////////////////////////////////////////////////////
static void TASK_MIDI(void *pvParameters);
static void TASK_Period1mS(void *pvParameters);
static void TASK_Period1mS_LowPrio(void *pvParameters);


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

 
/////////////////////////////////////////////////////////////////////////////
// Initialize all tasks
/////////////////////////////////////////////////////////////////////////////
s32 TASKS_Init(u32 mode)
{
  // create semaphores
  xSDCardSemaphore = xSemaphoreCreateRecursiveMutex();
  xMIDIINSemaphore = xSemaphoreCreateRecursiveMutex();
  xMIDIOUTSemaphore = xSemaphoreCreateRecursiveMutex();
  xLCDSemaphore = xSemaphoreCreateRecursiveMutex();
  xJ16Semaphore = xSemaphoreCreateRecursiveMutex();
  // TODO: here we could check for NULL and bring MBSEQ into halt state

  // start tasks (sizes are defined in mios32_config.h and have to be observed with the avstack.pl tool)
  xTaskCreate(TASK_MIDI,              "MIDI",         (MIDI_TASK_STACK_SIZE)/4,              NULL, PRIORITY_TASK_MIDI, NULL);
  xTaskCreate(TASK_Period1mS,         "Period1mS",    (PERIOD1MS_TASK_STACK_SIZE)/4,         NULL, PRIORITY_TASK_PERIOD1MS, NULL);
  xTaskCreate(TASK_Period1mS_LowPrio, "Period1mS_LP", (PERIOD1MS_LOWPRIO_TASK_STACK_SIZE)/4, NULL, PRIORITY_TASK_PERIOD1MS_LOW_PRIO, NULL);

#if !defined(MIOS32_DONT_USE_OSC)
  // finally init the lwIP task
  UIP_TASK_Init(0);
#endif

  return 0; // no error
}


/////////////////////////////////////////////////////////////////////////////
// This task is called periodically each mS as well
// it handles sequencer and MIDI events
/////////////////////////////////////////////////////////////////////////////
static void TASK_MIDI(void *pvParameters)
{
  TickType_t xLastExecutionTime;

  // Initialise the xLastExecutionTime variable on task entry
  xLastExecutionTime = xTaskGetTickCount();

  while( 1 ) {
    xTaskDelayUntil(&xLastExecutionTime, pdMS_TO_TICKS(1U));

    // skip delay gap if we had to wait for more than 5 ticks to avoid 
    // unnecessary repeats until xLastExecutionTime reached xTaskGetTickCount() again
    TickType_t xCurrentTickCount = xTaskGetTickCount();
    if( xLastExecutionTime < (xCurrentTickCount-5) )
      xLastExecutionTime = xCurrentTickCount;

    // continue in application hook
    SEQ_TASK_MIDI();
  }
}


/////////////////////////////////////////////////////////////////////////////
// This task is called periodically each mS
/////////////////////////////////////////////////////////////////////////////
static void TASK_Period1mS(void *pvParameters)
{
  TickType_t xLastExecutionTime;

  // Initialise the xLastExecutionTime variable on task entry
  xLastExecutionTime = xTaskGetTickCount();

  while( 1 ) {
    xTaskDelayUntil(&xLastExecutionTime, pdMS_TO_TICKS(1U));

    // skip delay gap if we had to wait for more than 5 ticks to avoid 
    // unnecessary repeats until xLastExecutionTime reached xTaskGetTickCount() again
    TickType_t xCurrentTickCount = xTaskGetTickCount();
    if( xLastExecutionTime < (xCurrentTickCount-5) )
      xLastExecutionTime = xCurrentTickCount;

    // continue in application hook
    SEQ_TASK_Period1mS();
  }
}


/////////////////////////////////////////////////////////////////////////////
// This task is called periodically each mS with low priority
/////////////////////////////////////////////////////////////////////////////
static void TASK_Period1mS_LowPrio(void *pvParameters)
{
  u16 ms_ctr = 0;

  while( 1 ) {
    // using vTaskDelay instead of xTaskDelayUntil, since a periodical execution
    // isn't required, and this task could be invoked too often if it was blocked
    // for a long time
    vTaskDelay(pdMS_TO_TICKS(1U));

    // continue in application hook
    SEQ_TASK_Period1mS_LowPrio();

    // 1 second task
    if( ++ms_ctr >= 1000 ) {
      ms_ctr = 0;
      // continue in application hook
      SEQ_TASK_Period1S();
    }
  }
}


/////////////////////////////////////////////////////////////////////////////
// functions to access J16 semaphore
// see also mios32_config.h
/////////////////////////////////////////////////////////////////////////////
void TASKS_J16SemaphoreTake(void)
{
  if( xJ16Semaphore != NULL )
    MUTEX_J16_TAKE;
}

void TASKS_J16SemaphoreGive(void)
{
  if( xJ16Semaphore != NULL )
    MUTEX_J16_GIVE;
}


/////////////////////////////////////////////////////////////////////////////
// functions to access MIDI IN/Out Mutex
// see also mios32_config.h
/////////////////////////////////////////////////////////////////////////////
void TASKS_MUTEX_MIDIOUT_Take(void) { MUTEX_MIDIOUT_TAKE; }
void TASKS_MUTEX_MIDIOUT_Give(void) { MUTEX_MIDIOUT_GIVE; }
void TASKS_MUTEX_MIDIIN_Take(void) { MUTEX_MIDIIN_TAKE; }
void TASKS_MUTEX_MIDIIN_Give(void) { MUTEX_MIDIIN_GIVE; }
