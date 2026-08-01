
This is a downstripped copy of FreeRTOS Kernel V11.3.0.

It only contains the required sources and GCC ports for building MIOS32 on
STM32 and LPC17 targets.

The complete version is available from:
https://github.com/FreeRTOS/FreeRTOS-Kernel/tree/V11.3.0


Modifications:

- MIOS32 retains a small pvPortRealloc compatibility helper in heap_4.c because
  the traditional programming model redirects the C allocator to FreeRTOS.
- The MIOS32 heap report is implemented outside the kernel in
  ../programming_models/traditional/freertos_heap.cpp using vPortGetHeapStats().
