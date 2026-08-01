# LPC17xx CMSIS and device support

The LPC17xx build combines the last official LPC1700 device definitions with a
current CMSIS core:

- `LPC17xx.h`, `system_LPC17xx.h`, and `system_LPC17xx.c` come from
  `Keil.LPC1700_DFP.2.7.2.pack`, released 2024-02-09 and deprecated by Keil on
  the same date. The downloaded pack has SHA-256
  `6de5a2386ae9f54b115a7146744d4de4297a4130803bf20ce7dd8be5c36dafe2`.
- The core and GNU compiler headers come from Arm CMSIS 6.3.0, tag `v6.3.0`,
  commit `45dab712ad84f8cbbf2b7bfc089c19088507df6f`.
- `LPC17xx.h` adds the CMSIS 6 processor feature macros for the LPC1769
  Cortex-M3 r2p0. Its device register definitions otherwise match the DFP.

MIOS32 supports the GNU Arm Embedded toolchain, so only the generic and GCC
compiler headers needed by `core_cm3.h` are vendored. CMSIS 6 implements its
core access functions in headers; the obsolete CMSIS 1.30 `core_cm3.c` is no
longer compiled.

The DFP device files retain their upstream license notices. The Apache-2.0
license applying to the CMSIS 6 files is in `LICENSE.txt`.
