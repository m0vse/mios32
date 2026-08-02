# MIOS32 continuous integration

Firmware applications are discovered from Makefiles which include the embedded
`include/makefile/common.mk` build. A direct application change builds that
application for its supported cores. Changes to MIOS32, drivers, modules,
FreeRTOS, linker scripts, programming models, bootloader integration, or board
environment files build every discovered firmware application.

`firmware-config.json` records hardware restrictions and the subset of mature
applications which already have release packaging scripts. Tutorials,
benchmarks, examples, and tests are continuously built, but do not create noisy
end-user releases.

Release tags are application scoped: `<application>-v<version>`. The first
automated release starts at the version recorded in the configuration. Later
successful releases increment the final numeric component independently for
each application. CI/release-only changes build the full matrix without
publishing releases.

The embedded build uses serial GNU make deliberately. Parallel native compiler
processes were one source of the historical Windows build unreliability, and a
serial CI baseline also gives deterministic failure logs while the legacy
applications are modernised.
