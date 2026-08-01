FatFs for MIOS32
================

FatFs is developed by ChaN.  The upstream license is in LICENSE.txt and the
upstream release notes are in src/00readme.txt and src/00history.txt.

Upstream source:
  https://elm-chan.org/fsw/ff/

Bundled version:
  R0.16 with upstream patches 1 and 2 (latest patch set published 2026-07-10)

MIOS32 integration changes:
  - src/diskio.c adapts the FatFs physical-drive interface to MIOS32_SDCARD.
  - src/mios32_fatfs.h exposes the R0.16 formatting adapter, including the
    required 512-byte temporary stack work buffer.
  - src/ffconf.h preserves the historical FAT12/16/32, 512-byte sector, and
    read/write configuration. exFAT and 64-bit LBA remain disabled.
  - FATFS_USE_LFN and FATFS_MAX_LFN may be set in an application's
    mios32_config.h. Long filename support remains disabled by default.
  - fatfs.mk compiles the upstream core, system, and Unicode support sources.

Validation still required on hardware:
  - mount/read/write/rename/delete and directory enumeration
  - clean and fragmented cards, full and read-only media
  - corrupt media and card removal during reads and writes
  - formatting from the terminal on STM32F1, STM32F4, and LPC17xx
