# 4. Re-send the panel init once after boot, and accept that it is blind

Date: 2026-11-05
Status: accepted

## Context

The board would occasionally come up with a dark panel while the application was running
normally: the ledger kept writing, the battery read fine, the network was up. Opening the
serial port made the panel light, because asserting DTR/RTS resets the chip and the init
sequence runs again.

Diagnosis: the ST7305 sometimes does not come up from the sequence sent in `Init()`. No
error is returned, no log line is produced, and the controller cannot be read back, so the
application has no way to tell a dark panel from a slow one. From outside, a dark panel with
a live application is indistinguishable from a dead board, and it was reported as "it will
not power on".

## Decision

`RlcdPanel::Repair()` re-sends the init sequence and pushes the frame, once, four seconds
after the first paint. It is unconditional: the firmware cannot know whether the panel is
dark, and the operation is idempotent and costs about 12 ms of SPI on a healthy one.

## Consequences

- A device that would have needed a reflash to be usable recovers by itself.
- The cause of the bad init is not established. A controller still settling at boot is the
  suspicion, which the four seconds also cover; if a panel still comes up dark with this in
  place, the next step is a longer reset pulse and a slower first init, not another retry.
- This is a workaround with a known trigger and an unknown mechanism, recorded here so it is
  not mistaken for a fix.
