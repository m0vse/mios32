#include <mios32.h>
#include "lwip/sys.h"

#if defined(MIOS32_FAMILY_LPC17xx)
unsigned char mios32_lwip_ram_heap[MEM_SIZE + 32]
#if !defined(MIOS32_LWIP_HEAP_IN_MAIN_SRAM)
  __attribute__((section(".bss_ahb"), aligned(MEM_ALIGNMENT)));
#else
  __attribute__((aligned(MEM_ALIGNMENT)));
#endif
#endif

u32_t sys_now(void)
{
  return MIOS32_TIMESTAMP_Get();
}
