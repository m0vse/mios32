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

#if defined(SEQ_NETWORK_USE_LWIP)
# include "lwip_task.h"
#else
# include "uip_task.h"
#endif

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
static void TASKS_WatchdogCheck(void);
static void TASKS_WatchdogHardwareInit(void);
static void TASKS_WatchdogHardwareFeed(void);


/////////////////////////////////////////////////////////////////////////////
// Local variables
/////////////////////////////////////////////////////////////////////////////

static u32 reset_source;
static volatile u32 watchdog_heartbeat[4];
static u32 watchdog_heartbeat_previous[4];
static volatile u32 watchdog_missing_mask;
static volatile TickType_t watchdog_suspend_until;
static volatile u8 watchdog_suspend_depth;
static u8 watchdog_enabled;


/////////////////////////////////////////////////////////////////////////////
// Returns the reset flags captured before TASKS_Init() cleared the hardware
// register.  Keeping this value allocation-free also makes it available to a
// later watchdog implementation.
/////////////////////////////////////////////////////////////////////////////
u32 TASKS_ResetSourceGet(void)
{
  return reset_source;
}


/////////////////////////////////////////////////////////////////////////////
// Records forward progress from one of the independently scheduled tasks.
/////////////////////////////////////////////////////////////////////////////
void TASKS_WatchdogHeartbeat(u8 source)
{
  if( source < (sizeof(watchdog_heartbeat) / sizeof(watchdog_heartbeat[0])) )
    ++watchdog_heartbeat[source];
}


/////////////////////////////////////////////////////////////////////////////
// Temporarily ignores missing task heartbeats for a bounded operation.  The
// deadline remains effective even if a caller hangs before Resume().
/////////////////////////////////////////////////////////////////////////////
void TASKS_WatchdogSuspend(u32 maximum_ms)
{
  if( maximum_ms > TASKS_WATCHDOG_LONG_OPERATION_MS )
    maximum_ms = TASKS_WATCHDOG_LONG_OPERATION_MS;

  TickType_t deadline = xTaskGetTickCount() + pdMS_TO_TICKS(maximum_ms);
  taskENTER_CRITICAL();
  if( watchdog_suspend_depth == 0 ||
      (s32)(deadline - watchdog_suspend_until) > 0 )
    watchdog_suspend_until = deadline;
  if( watchdog_suspend_depth < 0xff )
    ++watchdog_suspend_depth;
  taskEXIT_CRITICAL();
}


void TASKS_WatchdogResume(void)
{
  taskENTER_CRITICAL();
  if( watchdog_suspend_depth > 0 )
    --watchdog_suspend_depth;
  if( watchdog_suspend_depth == 0 )
    watchdog_suspend_until = 0;
  taskEXIT_CRITICAL();
}


s32 TASKS_WatchdogEnabled(void)
{
  return watchdog_enabled;
}


u32 TASKS_WatchdogMissingGet(void)
{
  return watchdog_missing_mask;
}


u32 TASKS_WatchdogSuspendRemainingGet(void)
{
  TickType_t now = xTaskGetTickCount();
  if( watchdog_suspend_depth == 0 ||
      (s32)(watchdog_suspend_until - now) <= 0 )
    return 0;

  return (u32)(watchdog_suspend_until - now) * portTICK_PERIOD_MS;
}


/////////////////////////////////////////////////////////////////////////////
// Feed only after every monitored task has advanced since the previous check.
// TASK_Period1mS is the supervisor, so reaching this function is its heartbeat.
/////////////////////////////////////////////////////////////////////////////
static void TASKS_WatchdogCheck(void)
{
  u32 missing = 0;
  unsigned i;

  for(i=0; i<(sizeof(watchdog_heartbeat) / sizeof(watchdog_heartbeat[0])); ++i) {
    u32 current = watchdog_heartbeat[i];
    if( current == watchdog_heartbeat_previous[i] )
      missing |= (1UL << i);
    watchdog_heartbeat_previous[i] = current;
  }

  TickType_t now = xTaskGetTickCount();
  if( watchdog_suspend_depth != 0 &&
      (s32)(watchdog_suspend_until - now) > 0 )
    missing = 0;

  watchdog_missing_mask = missing;
  if( watchdog_enabled && missing == 0 )
    TASKS_WatchdogHardwareFeed();
}


/////////////////////////////////////////////////////////////////////////////
// LPC17 watchdog: IRC is 4 MHz and the counter advances every four watchdog
// clocks.  12,000,000 counts therefore provides an approximately 12 second
// recovery window while staying below the 24-bit WDTC limit.
/////////////////////////////////////////////////////////////////////////////
static void TASKS_WatchdogHardwareInit(void)
{
#if MBSEQ_WATCHDOG_ENABLE && defined(MIOS32_FAMILY_LPC17xx)
  LPC_WDT->WDCLKSEL = 1; // internal RC oscillator
  LPC_WDT->WDTC = 12000000;
  LPC_WDT->WDMOD = (1 << 0) | (1 << 1); // enable watchdog and reset
  watchdog_enabled = 1;
  TASKS_WatchdogHardwareFeed();
#else
  watchdog_enabled = 0;
#endif
}


static void TASKS_WatchdogHardwareFeed(void)
{
#if MBSEQ_WATCHDOG_ENABLE && defined(MIOS32_FAMILY_LPC17xx)
  u32 interrupt_state = __get_PRIMASK();
  __disable_irq();
  LPC_WDT->WDFEED = 0xaa;
  LPC_WDT->WDFEED = 0x55;
  if( !interrupt_state )
    __enable_irq();
#endif
}


/////////////////////////////////////////////////////////////////////////////
// Initialize all tasks
/////////////////////////////////////////////////////////////////////////////
s32 TASKS_Init(u32 mode)
{
  s32 status = 0;
  TaskHandle_t midi_task = NULL;
  TaskHandle_t period_task = NULL;
  TaskHandle_t low_priority_task = NULL;

#if defined(MIOS32_FAMILY_LPC17xx)
  reset_source = LPC_SC->RSID;
  LPC_SC->RSID = reset_source & 0x3f; // write one to clear captured flags
#elif defined(MIOS32_FAMILY_STM32F10x) || defined(MIOS32_FAMILY_STM32F4xx)
  reset_source = RCC->CSR;
  RCC->CSR |= RCC_CSR_RMVF;
#else
  reset_source = 0;
#endif

  unsigned watchdog_source;
  for(watchdog_source=0;
      watchdog_source<(sizeof(watchdog_heartbeat) / sizeof(watchdog_heartbeat[0]));
      ++watchdog_source) {
    watchdog_heartbeat[watchdog_source] = 0;
    watchdog_heartbeat_previous[watchdog_source] = 0;
  }
  watchdog_missing_mask = 0;
  watchdog_suspend_until = 0;
  watchdog_suspend_depth = 0;
  watchdog_enabled = 0;

  // create semaphores
  xSDCardSemaphore = xSemaphoreCreateRecursiveMutex();
  xMIDIINSemaphore = xSemaphoreCreateRecursiveMutex();
  xMIDIOUTSemaphore = xSemaphoreCreateRecursiveMutex();
  xLCDSemaphore = xSemaphoreCreateRecursiveMutex();
  xJ16Semaphore = xSemaphoreCreateRecursiveMutex();

  if( xSDCardSemaphore == NULL || xMIDIINSemaphore == NULL ||
      xMIDIOUTSemaphore == NULL || xLCDSemaphore == NULL ||
      xJ16Semaphore == NULL ) {
    status = -1;
    goto cleanup;
  }

  // start tasks (sizes are defined in mios32_config.h and have to be observed with the avstack.pl tool)
  if( xTaskCreate(TASK_MIDI, "MIDI", (MIDI_TASK_STACK_SIZE)/4, NULL,
                  PRIORITY_TASK_MIDI, &midi_task) != pdPASS ) {
    status = -2;
    goto cleanup;
  }

  if( xTaskCreate(TASK_Period1mS, "Period1mS", (PERIOD1MS_TASK_STACK_SIZE)/4, NULL,
                  PRIORITY_TASK_PERIOD1MS, &period_task) != pdPASS ) {
    status = -3;
    goto cleanup;
  }

  if( xTaskCreate(TASK_Period1mS_LowPrio, "Period1mS_LP", (PERIOD1MS_LOWPRIO_TASK_STACK_SIZE)/4, NULL,
                  PRIORITY_TASK_PERIOD1MS_LOW_PRIO, &low_priority_task) != pdPASS ) {
    status = -4;
    goto cleanup;
  }

#if !defined(MIOS32_DONT_USE_OSC)
  // finally init the lwIP task
  UIP_TASK_Init(0);
#endif

  TASKS_WatchdogHardwareInit();

  return 0; // no error

cleanup:
  if( low_priority_task )
    vTaskDelete(low_priority_task);
  if( period_task )
    vTaskDelete(period_task);
  if( midi_task )
    vTaskDelete(midi_task);

  if( xJ16Semaphore )
    vSemaphoreDelete(xJ16Semaphore);
  if( xLCDSemaphore )
    vSemaphoreDelete(xLCDSemaphore);
  if( xMIDIOUTSemaphore )
    vSemaphoreDelete(xMIDIOUTSemaphore);
  if( xMIDIINSemaphore )
    vSemaphoreDelete(xMIDIINSemaphore);
  if( xSDCardSemaphore )
    vSemaphoreDelete(xSDCardSemaphore);

  xJ16Semaphore = NULL;
  xLCDSemaphore = NULL;
  xMIDIOUTSemaphore = NULL;
  xMIDIINSemaphore = NULL;
  xSDCardSemaphore = NULL;

  return status;
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
    if( (TickType_t)(xCurrentTickCount - xLastExecutionTime) > (TickType_t)5 )
      xLastExecutionTime = xCurrentTickCount;

    // continue in application hook
    SEQ_TASK_MIDI();
    TASKS_WatchdogHeartbeat(TASKS_WATCHDOG_HEARTBEAT_MIDI);
  }
}


/////////////////////////////////////////////////////////////////////////////
// This task is called periodically each mS
/////////////////////////////////////////////////////////////////////////////
static void TASK_Period1mS(void *pvParameters)
{
  TickType_t xLastExecutionTime;
  u16 watchdog_ms = 0;

  // Initialise the xLastExecutionTime variable on task entry
  xLastExecutionTime = xTaskGetTickCount();

  while( 1 ) {
    xTaskDelayUntil(&xLastExecutionTime, pdMS_TO_TICKS(1U));

    // skip delay gap if we had to wait for more than 5 ticks to avoid 
    // unnecessary repeats until xLastExecutionTime reached xTaskGetTickCount() again
    TickType_t xCurrentTickCount = xTaskGetTickCount();
    if( (TickType_t)(xCurrentTickCount - xLastExecutionTime) > (TickType_t)5 )
      xLastExecutionTime = xCurrentTickCount;

    // continue in application hook
    SEQ_TASK_Period1mS();

    if( ++watchdog_ms >= 1000 ) {
      watchdog_ms = 0;
      TASKS_WatchdogCheck();
    }
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
    TASKS_WatchdogHeartbeat(TASKS_WATCHDOG_HEARTBEAT_LOW_PRIO);

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
