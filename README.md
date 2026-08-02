# MIOS32 modernization fork

[![Firmware](https://github.com/m0vse/mios32/actions/workflows/firmware.yml/badge.svg)](https://github.com/m0vse/mios32/actions/workflows/firmware.yml)
[![MIOS Studio](https://github.com/m0vse/mios32/actions/workflows/mios-studio.yml/badge.svg)](https://github.com/m0vse/mios32/actions/workflows/mios-studio.yml)

This repository contains the MIOS32 platform and the MIDIbox applications built
on it. This fork is modernizing that codebase so that it remains practical to
build, maintain, and run with current embedded and desktop toolchains.

The first hardware validation target is **MIDIbox SEQ V4**, including the
LPC17xx platform. MIDIbox SEQ V4 is the current focus, not the limit of the
project: the intention is to update all MIOS32 applications, modules, platform
drivers, tools, and supported boards over time.

## What are MIDIbox and MIOS32?

[MIDIbox](https://www.midibox.org/) is an open hardware and software platform
for building dedicated MIDI controllers, sequencers, synthesizers, routers,
and related musical devices. Its projects combine reusable core boards and I/O
modules with application firmware that musicians can adapt to their own
hardware.

MIOS32 is the 32-bit embedded operating environment used by many MIDIbox
projects. It provides a common API for MIDI, USB, Ethernet, storage, displays,
controls, timers, and other peripherals above supported microcontroller
platforms. Applications can therefore share the same modules and much of the
same source across boards.

[MIDIbox SEQ V4](apps/sequencers/midibox_seq_v4/) is a hardware MIDI step
sequencer built on MIOS32. It exercises a particularly broad part of the
platform—including real-time scheduling, USB MIDI, multiple hardware MIDI
ports, SD-card storage, Ethernet/OSC, displays, and control-surface I/O—making
it a useful first integration and hardware-test target for this work.

[MIOS Studio](tools/mios_studio/) is the companion desktop application used to
discover and query MIDIbox devices, upload firmware, access the MIOS terminal,
monitor MIDI traffic, and use device-specific tools.

## Goals of this fork

- Keep MIOS32 and its applications buildable with supported contemporary
  compilers, SDKs, and desktop development environments.
- Update foundational dependencies such as FreeRTOS, FatFs, lwIP, CMSIS,
  vendor device support, TinyUSB, and JUCE while preserving MIOS32 APIs where
  practical.
- Modernize and validate the STM32 and LPC17xx platform drivers, peripheral
  drivers, modules, build system, and bootloader path.
- Modernize MIOS Studio, including a reproducible Visual Studio 2022 build,
  current JUCE, reliable MIDI-device re-enumeration, and firmware upload across
  application/bootloader USB transitions.
- Preserve the timing and resource constraints of the embedded targets rather
  than treating a successful compile as sufficient validation.
- Test changes on real MIDIbox hardware, initially MIDIbox SEQ V4 on LPC17xx,
  before applying proven platform changes more broadly.
- Keep architectural changes separate and reviewable, with one focused commit
  for each dependency update, driver migration, or behavioural change.

## Scope and current priority

Work is proceeding from shared foundations outward:

1. Make the build reproducible with current toolchains.
2. Update shared kernels, filesystems, network stacks, device support, and
   peripheral drivers.
3. Validate those changes through MIDIbox SEQ V4 on STM32 and LPC17xx hardware.
4. Apply and test the updated platform across the other applications in
   [`apps/`](apps/), along with reusable code in [`modules/`](modules/) and
   [`drivers/`](drivers/).
5. Modernize the bootloader and desktop workflow once the application-side
   replacements are stable enough to provide a safe migration path.

The detailed dependency baseline, completed migrations, remaining hardware
tests, and known constraints are tracked in the
[MIDIbox SEQ V4 dependency modernization notes](apps/sequencers/midibox_seq_v4/DEPENDENCY_MODERNIZATION.md).

## Repository layout

- [`apps/`](apps/) — MIDIbox applications, examples, templates, and tests
- [`mios32/`](mios32/) — the common MIOS32 API and core implementation
- [`drivers/`](drivers/) — microcontroller and host platform support
- [`modules/`](modules/) — reusable embedded middleware and hardware modules
- [`bootloader/`](bootloader/) — MIOS32 bootloader and updater sources
- [`tools/`](tools/) — MIOS Studio and supporting desktop/command-line tools
- [`FreeRTOS/`](FreeRTOS/) — the embedded real-time kernel used by MIOS32

## Building and testing

Build requirements vary by target. Existing application makefiles remain the
primary embedded entry points, while MIOS Studio now also has a CMake/Visual
Studio 2022 build in [`tools/mios_studio/`](tools/mios_studio/). Until the
documentation is consolidated, consult the README and makefile alongside the
specific application or tool you are building.

GitHub Actions discovers every embedded application Makefile and builds only
the affected applications for normal changes. Changes to shared MIOS32 code,
drivers, modules, FreeRTOS, or build infrastructure expand that check to the
complete application set on LPC17xx, STM32F1, and STM32F4 where supported.
MIOS Studio is built with JUCE on Windows, Linux, and macOS.

Successful main-branch builds of applications in the official
[MIOS32 download catalogue](https://ucapps.de/mios32_download.html) produce
independently versioned GitHub Releases. Each firmware archive contains LPC17,
STM32F1, and STM32F4 images, and the calculated release version is embedded in
the firmware boot/SysEx identity. Tags include the application name, for
example `midibox_seq_v4-v4.100` and `mios_studio-v2.4.13`; other applications,
tutorials, and tests are continuously built without creating end-user releases.

Embedded changes should be checked for both STM32 and LPC17xx where applicable.
For MIDIbox SEQ V4, compile/link success should be followed by hardware checks
covering boot, MIDI I/O, USB enumeration and firmware upload, SD-card access,
Ethernet/DHCP, and representative control-surface operation.

## Project history and licensing

MIOS32 and the MIDIbox applications are the work of their original authors and
contributors. This fork builds on that long-running project; modernization
commits should retain existing copyright and licence notices. The repository
contains components under different licences, so consult the notices attached
to the relevant source or dependency before redistributing a build or reusing
code.

For background, hardware documentation, and the wider community, visit the
[MIDIbox website](https://www.midibox.org/) and
[MIDIbox forum](https://midibox.org/forums/).
