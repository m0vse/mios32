# $Id$
# defines additional rules for MIOS32 family

# select driver library
DRIVER_LIB =	$(MIOS32_PATH)/drivers/$(FAMILY)

# enhance include path
C_INCLUDE += -I $(MIOS32_PATH)/mios32/$(FAMILY) \
	     -I $(DRIVER_LIB)/CMSIS/inc \
	     -I $(DRIVER_LIB)/iap/inc \
	     -I $(DRIVER_LIB)/usbstack/inc

# add modules to thumb sources
ifeq ($(MIOS32_USB_USE_TINYUSB),1)
CFLAGS += -DMIOS32_USB_USE_TINYUSB=1
C_INCLUDE += -I $(MIOS32_PATH)/mios32/$(FAMILY)/tinyusb \
	     -I $(MIOS32_PATH)/modules/tinyusb/src
THUMB_SOURCE += \
	$(MIOS32_PATH)/modules/tinyusb/src/tusb.c \
	$(MIOS32_PATH)/modules/tinyusb/src/common/tusb_fifo.c \
	$(MIOS32_PATH)/modules/tinyusb/src/device/usbd.c \
	$(MIOS32_PATH)/modules/tinyusb/src/class/midi/midi_device.c \
	$(MIOS32_PATH)/modules/tinyusb/src/portable/nxp/lpc17_40/dcd_lpc17_40.c
else
THUMB_SOURCE += \
	$(DRIVER_LIB)/usbstack/src/usbhw_lpc.c \
	$(DRIVER_LIB)/usbstack/src/usbcontrol.c \
	$(DRIVER_LIB)/usbstack/src/usbstdreq.c \
	$(DRIVER_LIB)/usbstack/src/usbinit.c
endif


THUMB_AS_SOURCE += 

# directories and files that should be part of the distribution (release) package
DIST += $(MIOS32_PATH)/mios32/$(FAMILY)
