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
- The standard uIP integration adds another FreeRTOS task and a global recursive
  mutex.  It services DHCP and the UDP-based OSC endpoints.
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
| uIP | uIP 1.0 historical sources | No maintained drop-in uIP release | Replace with a maintained stack rather than relabeling the historical code. |
| STM32F4 support | StdPeriph V1.1.0-era tree and legacy USB libraries | STM32CubeF4 V1.28.3; legacy SPL V1.9.0 | Port to Cube HAL/LL and Cube USB as an STM32F4-only project. |
| STM32F1 support | StdPeriph V3.3.0 and legacy USB library | STM32CubeF1 V1.8.7; legacy SPL V3.6.x | Port separately from F4; retain F103/F105 target coverage. |
| LPC17xx support | CMSIS 1.30-era device layer and custom legacy USB stack | LPCOpen 2.10 / LPC1700 DFP 2.7.2 | Port as an LPC17xx-only project; current NXP sources require account/license retrieval. |
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

## Verified build matrix

Built with GNU Arm Embedded Toolchain 14.2.Rel1 and GNU Make 4.4.1:

| Board environment | Processor | Text | Data | BSS | Result |
| --- | --- | ---: | ---: | ---: | ---: |
| `source_me_MBHP_CORE_STM32F4` | STM32F407VG | 430368 | 960 | 73440 | Pass |
| `source_me_MBHP_CORE_STM32` | STM32F103RE | 414804 | 952 | 59768 | Pass |
| `source_me_MBHP_CORE_LPC17` | LPC1769 | 408260 | 904 | 62824 | Pass |

Clean direct parallel builds (`make -j4`) are verified for all three board
environments. The common rules remain compatible with the GNU Make 3.81/MSYS
baseline documented by MIDIbox as well as the tested GNU Make 4.4.1 environment.

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

### 3. Replace uIP 1.0

SEQ V4 primarily needs DHCP and UDP OSC, so a minimal lwIP 2.2.x port is a
smaller migration than reproducing every uIP feature.  Preserve the public OSC
module boundary, replace the uIP global mutex, and test packet bursts without
disturbing MIDI timing.  FreeRTOS+TCP is another viable option but couples the
network migration more tightly to the RTOS.

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

### 6. Port LPC17xx to LPCOpen

Replace the CMSIS 1.30-era device layer, direct peripheral setup, Ethernet MAC,
and USB stack behind the existing MIOS32 API.  LPCOpen 2.10 is the latest NXP
package for LPC1769, but it is also old and account-gated; archive its exact
package and checksum when it is imported.  Keep this work independent of both
STM32 family migrations.

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
- The uIP protothread macros emit modern-compiler warnings and the stack has no
  maintained upstream security line.
- The repository-wide application license permits personal non-commercial use
  only.  Confirm redistribution terms before publishing combined binaries or
  refreshed proprietary vendor code.
