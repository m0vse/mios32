# MIDIbox SEQ V4 dependency modernization

Assessment date: 2026-08-01

This document records the modernization baseline for the embedded MIDIbox SEQ
V4 application.  It deliberately separates dependency upgrades by subsystem and
MCU family: storage, networking, USB, and MCU support all need their own hardware
validation and must not be hidden inside an RTOS update.

## Runtime architecture

- `modules/sequencer/seq_bpm.c` produces clock, transport, and song-position
  requests from the timer/MIDI timing side.
- The high-priority 1 ms `TASK_MIDI` loop in `mios32/tasks.c` drains those
  requests through `SEQ_TASK_MIDI()` and `SEQ_CORE_Handler()`.
- The UI/storage work runs in two lower-priority 1 ms tasks.  Recursive mutexes
  serialize SD card, MIDI input/output, LCD, and J16 bus access.
- The lwIP raw API runs in one dedicated FreeRTOS task behind a recursive mutex.
  It services Ethernet polling, ARP, IPv4, ICMP, DHCP, and UDP-based OSC without
  a second lwIP TCP/IP thread.
- The traditional programming model owns the repository-wide FreeRTOS
  configuration and redirects the C/C++ allocator to FreeRTOS `heap_4`.

The MIDI clock path is therefore sensitive to scheduler, interrupt-priority, and
mutex changes.  A successful link is necessary but does not replace timing,
USB-MIDI, Ethernet, and SD-card tests on each board family.

## Dependency baseline

| Subsystem | Previous/current bundled version | Current upstream checked | Decision |
| --- | --- | --- | --- |
| FreeRTOS kernel | V9.0.0 / V11.3.0 after modernization | V11.3.0 (March 2026) | Updated and cross-target compiled. |
| FatFs | R0.07e / R0.16 patch 2 after modernization | R0.16 patch 2 (July 2026) | Updated and cross-target compiled; hardware/media fault testing remains. |
| Network stack | uIP 1.0 / lwIP 2.2.1 after modernization | lwIP 2.2.1 (February 2025) | Replaced for SEQ V4 and cross-target compiled; hardware network testing remains. |
| STM32F4 support | StdPeriph V1.1.0-era tree and legacy USB libraries | STM32CubeF4 V1.28.3; legacy SPL V1.9.0 | Port to Cube HAL/LL and Cube USB as an STM32F4-only project. |
| STM32F1 support | StdPeriph V3.3.0 and legacy USB library | STM32CubeF1 V1.8.7; legacy SPL V3.6.x | Port separately from F4; retain F103/F105 target coverage. |
| LPC17xx support | CMSIS 1.30-era device layer and custom legacy USB stack | LPCOpen 2.10 / LPC1700 DFP 2.7.2; CMSIS-Core 6.3.0; TinyUSB 0.21.0 | Use the last official LPC1769 device support as a reference, then modernize maintained components behind the MIOS32 API. |
| MIOS Studio / JUCE | External `~/JUCE/modules`; no pinned revision | JUCE 9.0.0 (July 2026) | Add reproducible desktop checks, then upgrade JUCE as an independent migration. |

## Completed changes

1. The FreeRTOS kernel and the selected GCC Cortex-M3/Cortex-M4F ports were
   replaced with the official V11.3.0 release.
2. MIOS32 allocator compatibility was retained without carrying the old fork of
   the complete heap implementation.  `calloc` and heap statistics now use
   upstream kernel APIs; the required `realloc` adapter remains small and local.
3. The embedded programming model, SEQ V4 tasks, and standard uIP task now use
   current FreeRTOS types and timing APIs with backward aliases disabled.
4. The LPC17xx USB vendor-response buffer was given static lifetime after GCC 14
   diagnosed a returned pointer to automatic storage.
5. FatFs was upgraded from R0.07e to R0.16 with upstream patches 1 and 2.
   MIOS32 disk I/O, formatting, file-object snapshots, directory listings, the
   STM32F4 USB-host adapter, and the SEQ V4 desktop shims were migrated to the
   current API. exFAT and 64-bit LBA remain disabled.
6. The common make graph now orders output-directory creation, linking, symbol
   generation, and build reporting correctly under parallel make. GCC writes
   dependency files directly; failed recipes no longer leave partial targets.
7. SEQ V4 now uses lwIP 2.2.1 instead of uIP 1.0. The minimal raw-API port keeps
   one network task and enables only ARP, IPv4, ICMP, DHCP, and UDP. OSC retains
   its four logical connections, including shared local ports and broadcast.
   STM32F1/F4 ENC28J60 and LPC17xx EMAC adapters now use stack-neutral frame
   buffers instead of uIP globals.

## Verified build matrix

Built with GNU Arm Embedded Toolchain 14.2.Rel1 and GNU Make 4.4.1:

| Board environment | Processor | Text | Data | BSS | Result |
| --- | --- | ---: | ---: | ---: | ---: |
| `source_me_MBHP_CORE_STM32F4` | STM32F407VG | 440664 | 960 | 73488 | Pass |
| `source_me_MBHP_CORE_STM32` | STM32F103RE | 425100 | 952 | 59816 | Pass |
| `source_me_MBHP_CORE_LPC17` | LPC1769 | 418276 | 904 | 62384 | Pass |

Clean direct builds (`make -j4`) are verified for all three board environments.
On Windows/MSYS the common rules serialize native GCC execution by default,
because parallel compiler workers were observed terminating silently and leaving
empty `.su` files. Set `MIOS32_ALLOW_PARALLEL_BUILD=1` only after validating a
different Windows toolchain. Other hosts retain normal make parallelism. The
rules remain compatible with the GNU Make 3.81/MSYS baseline documented by
MIDIbox and the tested GNU Make 4.4.1 environment.

## Recommended independent migrations

### 1. Add hardware regression coverage

Before replacing hardware libraries, capture MIDI-clock jitter, multi-port USB
MIDI enumeration/traffic, SD-card session load/save, DHCP, and OSC send/receive
on STM32F4 and LPC17xx hardware.  Keep binary-size and stack-usage reports for
each target.

### 2. Validate the FatFs R0.16 migration on hardware

The source migration and cross-target builds are complete. Test clean,
fragmented, full, read-only, and corrupt cards, plus unexpected removal during
reads and writes. Exercise terminal formatting on each MCU family. Do not enable
exFAT unless its additional memory and licensing behavior is explicitly wanted.

### 3. Validate lwIP 2.2.1 on hardware

Exercise DHCP acquisition and renewal, static addressing, link loss/recovery,
ICMP echo, all four OSC connections, duplicate local OSC ports, broadcast,
source filtering, SysEx fragmentation, and packet bursts while measuring MIDI
clock jitter. The LPC17xx configuration deliberately uses a 768-byte lwIP heap,
an allocation-free receive path, and the existing 1024-byte EMAC frame limit;
monitor memory-allocation failures and both RAM-bank margins under stress.

### 4. Port STM32F4 to STM32Cube

Replace the StdPeriph, legacy USB device/host, and OTG layers as one coherent
family port using a version-consistent CubeF4 set.  A CMSIS-header-only update is
not safe: current device headers select part-specific definitions and are tested
with matching Cube drivers, while MIOS32 directly compiles the old driver APIs.
Keep the existing MIOS32 public driver API so the sequencer core remains
unchanged.

### 5. Port STM32F1 independently

Do not share the F4 conversion commit.  The F103 and F105 USB/peripheral paths
and memory constraints differ, and both need independent firmware images and
hardware tests.

### 6. Modernize LPC17xx support

Do not treat the current MCUXpresso SDK as an LPC1769 upgrade source.
`mcuxsdk-core` contains shared drivers and build infrastructure rather than a
complete device SDK, and the current `mcux-devices-lpc` repository supports the
LPC51U68, LPC54000, LPC5500, and LPC800 families, not LPC17xx.  Adding LPC1769
would therefore create an unofficial device port rather than consume supported
NXP code.

Use LPCOpen 2.10 and LPC1700 DFP 2.7.2 as the last official LPC1769 device and
peripheral references.  Refresh the common core layer to CMSIS-Core 6.3.0 and
replace the application USB implementation with TinyUSB 0.21.0 behind the
existing MIOS32 API, first for MIDI/CDC and then for mass storage.  Audit direct
peripheral drivers individually against the NXP references instead of importing
an unsupported SDK wholesale.  Keep the device-layer, USB, Ethernet, and other
peripheral changes in separate commits with LPC1769 hardware validation after
each step.  Reserve MCUXpresso SDK integration for a future migration to an MCU
family that its device repositories actually support.

### 7. Add MIOS Studio checks and update JUCE

Make the desktop tool reproducible before changing its framework: pin JUCE,
generate/build the Windows, macOS, and Linux projects in CI, and add focused
checks for MIDI port discovery, firmware upload, terminal SysEx, file browsing,
and OSC. Then migrate MIOS Studio to JUCE 9.0.0 (or the latest stable release at
implementation time) in its own commit, including the JUCE 9 breaking changes
and licensing review. This is a roadmap item, not an ordering decision.

## Known risks found during compilation

- FreeRTOS Cortex-M ports only perform several vector-table and priority checks
  when `configASSERT` is enabled.  Assertions and task/mutex creation failure
  handling should be enabled in a separate reliability commit.
- The legacy STM32 USB code emits type and initialization warnings with GCC 14.
- lwIP compile/link coverage cannot validate PHY/MAC operation, DHCP renewal,
  OSC interoperability, or timing under packet bursts; all need board testing.
- The repository-wide application license permits personal non-commercial use
  only.  Confirm redistribution terms before publishing combined binaries or
  refreshed proprietary vendor code.
