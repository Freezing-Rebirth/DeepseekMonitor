# 2. Test the arithmetic on the host, not on the board

Date: 2026-11-05
Status: accepted

## Context

Before this decision the project had no tests at all. Verifying a change meant flashing the
board and reading the panel, which is slow, requires the hardware, and cannot show a wrong
number that merely looks plausible.

Every piece of arithmetic worth testing was nevertheless unreachable: `pricing.cpp` needed
the holiday subsystem (and therefore HTTPS and NVS) to answer whether today is a peak day;
the battery curve and the gauge were `static` in a file that also drove the ADC; the ledger
called `nvs_open` before every entry point; the balance and holiday parsers were `static` in
files that also opened sockets.

## Decision

Arithmetic moves into `*_rules`, `*_curve`, `*_math` and `*_json` translation units with no
ESP-IDF dependency, and `firmware/test/` builds them with a host compiler and runs them
under `ctest`. Ports to the hardware stay where they are and call into them.

Where a pure function needs a fact from the environment, the caller supplies it: the tariff
rules take the calendar as two booleans, the gauge takes the stored percentage, the ledger
takes the totals struct. Nothing reaches back out.

Every suite is verified by mutating the production code and watching a test fail. A suite
that cannot be made to fail is treated as broken, not as passing. This caught one real
defect in the tests themselves: an assertion written against the constant it was meant to
pin.

## Consequences

- `cmake -S firmware/test -B build && ctest` gives a red/green answer in under a second.
- The tests need a host compiler. On this machine that is a portable mingw in
  `%LOCALAPPDATA%`; the ESP-IDF toolchain is a cross-compiler and cannot produce host
  binaries.
- Two suites need cJSON, so the component manager must have fetched it once
  (`idf.py build` in `firmware/`). The CMake warns and skips rather than failing everything.
- Anything that touches hardware is still verified only on the board. This decision narrows
  that surface, it does not remove it.
