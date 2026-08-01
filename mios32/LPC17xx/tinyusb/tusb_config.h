#ifndef _MIOS32_LPC17XX_TUSB_CONFIG_H
#define _MIOS32_LPC17XX_TUSB_CONFIG_H

#define CFG_TUSB_MCU             OPT_MCU_LPC175X_6X
#define CFG_TUSB_OS              OPT_OS_NONE
#define CFG_TUSB_DEBUG           0

#define CFG_TUSB_RHPORT0_MODE    (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUD_ENABLED          1
#define CFG_TUD_ENDPOINT0_SIZE   64

#define CFG_TUD_CDC              0
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             1
#define CFG_TUD_VENDOR           0

#define CFG_TUD_MIDI_RX_BUFSIZE  64
#define CFG_TUD_MIDI_TX_BUFSIZE  64
#define CFG_TUD_MIDI_RX_EPSIZE   64
#define CFG_TUD_MIDI_TX_EPSIZE   64

/* LPC17xx USB DMA cannot access the CPU-local SRAM at 0x10000000. */
#define CFG_TUSB_MEM_SECTION     __attribute__((section(".bss_ahb")))
#define CFG_TUSB_MEM_ALIGN       __attribute__((aligned(4)))

#endif /* _MIOS32_LPC17XX_TUSB_CONFIG_H */
