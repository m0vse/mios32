// $Id$
//////////////////////////////////////////////////////////////////////////////
// as proposed by http://www.state-machine.com/arm/AN_QP_and_ARM7_ARM9-GNU.pdf
//
// but instead of disabling malloc and free, we overload these with the
// FreeRTOS heap_4 allocator.
//////////////////////////////////////////////////////////////////////////////

#include <stdlib.h>

#include <FreeRTOS.h>
#include <portmacro.h>
#include <mios32.h>

extern "C" void *pvPortRealloc(void *p, size_t size);

//............................................................................
extern "C" void *malloc(size_t size)
{
  return pvPortMalloc(size);
}

//............................................................................
extern "C" void *calloc(size_t count, size_t size)
{
  return pvPortCalloc(count, size);
}

//............................................................................
extern "C" void *realloc(void *p, size_t size)
{
  return pvPortRealloc(p, size);
}
//............................................................................
extern "C" void free(void *p)
{
  vPortFree(p);
}

//............................................................................
// Used by the MIOS32 "memory" terminal command.
extern "C" void vPortMallocDebugInfo(void)
{
  HeapStats_t stats;
  vPortGetHeapStats(&stats);

  const size_t used = configTOTAL_HEAP_SIZE - stats.xAvailableHeapSpaceInBytes;
  const size_t percent = (configTOTAL_HEAP_SIZE != 0U) ? (used * 100U) / configTOTAL_HEAP_SIZE : 0U;

  MIOS32_MIDI_SendDebugMessage(
    "Heap: %u of %u bytes used (%u%%), %u bytes free, %u bytes minimum ever free\n",
    (unsigned)used,
    (unsigned)configTOTAL_HEAP_SIZE,
    (unsigned)percent,
    (unsigned)stats.xAvailableHeapSpaceInBytes,
    (unsigned)stats.xMinimumEverFreeBytesRemaining);
  MIOS32_MIDI_SendDebugMessage(
    "Heap blocks: %u free, largest %u bytes, smallest %u bytes; allocations %u, frees %u\n",
    (unsigned)stats.xNumberOfFreeBlocks,
    (unsigned)stats.xSizeOfLargestFreeBlockInBytes,
    (unsigned)stats.xSizeOfSmallestFreeBlockInBytes,
    (unsigned)stats.xNumberOfSuccessfulAllocations,
    (unsigned)stats.xNumberOfSuccessfulFrees);
}
