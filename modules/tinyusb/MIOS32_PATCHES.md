# MIOS32 TinyUSB patch set

The vendored TinyUSB baseline is release **0.21.0**
(`dae3f9a366bfcddbf9dcf1b48d7500286a849539`). MIOS32 carries the following
targeted post-release backports for the LPC17xx device controller:

- `36cd9f9f46ca20be907ed57b874d9d1dc7b3bf64` — retain the non-isochronous
  EP0 control-OUT fix. It explicitly tracks a queued status-stage ZLP and
  clears stale control state when a new SETUP packet arrives.
- `a3ee0b4ff12615552de50bd2a61287fdb0b11bd9` — mask the USB interrupt around
  non-reentrant LPC17/40 SIE, endpoint-realisation, control FIFO, and endpoint
  interrupt-enable sequences shared by TinyUSB task and ISR contexts.

The isochronous endpoint support introduced by the first upstream commit is
not included because the MIOS32 LPC17xx MIDI configuration enables no
isochronous USB class. Keeping the backport to the control and concurrency
fixes avoids unrelated driver expansion while preserving the fixes relevant
to MIDI enumeration and bulk endpoint progress.

The rest of the TinyUSB history after 0.21.0 was audited through upstream
master. At the time of this backport, the other changes affecting the selected
source areas were limited to MIDI 2.0, vendor/isochronous class handling,
empty-driver warning fixes, type-cleanup, and licence-header conversion; none
is active in the MIOS32 MIDI 1.0-only configuration.
