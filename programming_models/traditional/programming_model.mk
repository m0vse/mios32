# $Id$
# defines rules building the programming model

# where is FreeRTOS located

FREE_RTOS      =    $(MIOS32_PATH)/FreeRTOS

# heap_4 remains the default for existing applications. Applications which
# provide multiple non-contiguous RAM regions can opt into heap_5 before this
# makefile is included.
MIOS32_FREERTOS_HEAP_TYPE ?= 4

ifeq ($(MIOS32_FREERTOS_HEAP_TYPE),5)
CFLAGS += -DMIOS32_FREERTOS_HEAP_5=1
FREERTOS_HEAP_SOURCE = $(FREE_RTOS)/Source/portable/MemMang/heap_5.c
else
FREERTOS_HEAP_SOURCE = $(FREE_RTOS)/Source/portable/MemMang/heap_4.c
endif

# extend include path
C_INCLUDE += 	-I $(MIOS32_PATH)/programming_models/traditional \
		-I $(FREE_RTOS)/Source/include \
		-I $(FREE_RTOS)/Source/portable/GCC/ARM_CM3 \
		-I $(FREE_RTOS)/Source/portable/MemMang \

# required by FreeRTOS to select the port
CFLAGS    +=    -DGCC_ARMCM3

# add modules to thumb sources
THUMB_SOURCE += \
		$(MIOS32_PATH)/programming_models/traditional/main.c \
		$(MIOS32_PATH)/programming_models/traditional/strtol.c \
		$(FREE_RTOS)/Source/tasks.c \
		$(FREE_RTOS)/Source/list.c \
		$(FREE_RTOS)/Source/queue.c \
		$(FREE_RTOS)/Source/timers.c \
		$(FREE_RTOS)/Source/portable/GCC/ARM_CM3/port.c \
		$(FREERTOS_HEAP_SOURCE)

ifeq ($(FAMILY),STM32F10x)
ifeq ($(PROCESSOR),STM32F103CB)
THUMB_SOURCE += $(MIOS32_PATH)/programming_models/traditional/startup_stm32f10x_md.c
else
THUMB_SOURCE += $(MIOS32_PATH)/programming_models/traditional/startup_stm32f10x_hd.c
endif
endif
ifeq ($(FAMILY),STM32F4xx)
THUMB_SOURCE += $(MIOS32_PATH)/programming_models/traditional/startup_stm32f4xx.c
endif
ifeq ($(FAMILY),LPC17xx)
THUMB_SOURCE += $(MIOS32_PATH)/programming_models/traditional/startup_LPC17xx.c
endif

THUMB_CPP_SOURCE += $(MIOS32_PATH)/programming_models/traditional/mini_cpp.cpp \
		    $(MIOS32_PATH)/programming_models/traditional/freertos_heap.cpp

# add MIOS32 sources
include $(MIOS32_PATH)/mios32/mios32.mk

# directories and files that should be part of the distribution (release) package
DIST += $(MIOS32_PATH)/programming_models/traditional
