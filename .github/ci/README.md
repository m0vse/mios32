# MIOS32 continuous integration

Firmware applications are discovered from Makefiles which include the embedded
`include/makefile/common.mk` build, then restricted to the ten applications in
the official MIOS32 download catalogue. A direct catalogue-application change
builds that application. Changes to shared firmware code build all ten.

`firmware-config.json` records the catalogue applications, their release assets,
and the three required platforms. Non-catalogue tutorials, benchmarks,
examples, quick experiments, and tests are outside the automated build matrix.

Release tags are application scoped: `<application>-v<version>`. The first
automated release starts at the version recorded in the configuration. Later
successful releases increment the final numeric component independently for
each application. CI/release-only changes build the ten-app matrix without
publishing releases.

The embedded build uses serial GNU make deliberately. Parallel native compiler
processes were one source of the historical Windows build unreliability, and a
serial CI baseline also gives deterministic failure logs while the legacy
applications are modernised.
