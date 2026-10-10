<p align="center">
  <b>EN</b>
</p>

<p align="center">
  <h1 align="center">DeepSeek Monitor</h1>
  <p align="center">A desk instrument for your DeepSeek account balance and token price,
  on a 4.2" reflective monochrome LCD.</p>
</p>

<br/>

<p align="center">
  <a href="#overview" title="What this is">Overview</a> •
  <a href="#the-panel" title="What it looks like">The panel</a> •
  <a href="#hardware" title="What you need">Hardware</a> •
  <a href="#build-and-flash" title="How to build it">Build and flash</a> •
  <a href="#first-time-setup" title="Getting it online">Setup</a> •
  <a href="#how-the-numbers-are-derived" title="BURN, RUNWAY and the token estimate">How the numbers work</a> •
  <a href="#source-layout" title="What each file does">Source layout</a>
</p>

## Overview

**DeepSeek Monitor** turns a Waveshare ESP32-S3-RLCD-4.2 board into a small always-on
display for a DeepSeek API account. It shows the account balance, the token equivalent of
that balance, the current tariff state, and the time of the next price change.

**Requirements**: an ESP-IDF v6.1 installation, the board, and a DeepSeek API key. No
server, no cloud service, and no companion app — the board talks to
`api.deepseek.com` directly over HTTPS.

**Why a reflective panel**: the ST7305 keeps its image with no backlight and no
continuous refresh, and it is legible in daylight. That suits a display that sits on a
desk all day showing one number.

**One screen, no interaction**: there are no menus. The panel shows the balance and the
tariff state; holding the BOOT button reopens the WiFi setup page. Everything else is
automatic.

## The panel

![DeepSeek Monitor](docs/panel.png)

```
■ DEEPSEEK                 WIFI:OK BAT:94% 12:04 UTC+8
TOTAL AVAILABLE FUNDS                        RUNWAY: 6D
¥2.83                                    141.50M TOKENS
BASIS: ¥0.02/M                             BURN: ¥0.46/D
───────────────────────────────────────────────────────
                       TROUGH
                    NEXT: 10-08 09:00
```

The layout is a 400×300 canvas in three bands: a 26 px status bar, a 124 px balance block,
and a 150 px pricing block. Only the pricing block inverts — dark for **PEAK**, light for
**TROUGH** — so the tariff state is readable from across a room.

All wording, positions, and font sizes come from the design exports in
`stitch_pixel_deepseek_status_display/`. The design is set in **Silkscreen**, and the panel
ships the same typeface, converted to 1bpp at the sizes the design specifies.

## Hardware

| Item | Value |
|---|---|
| Board | Waveshare ESP32-S3-RLCD-4.2 |
| Panel | ST7305 reflective monochrome, **400×300 landscape**, 1 bpp, SPI |
| Panel pins | CLK=GPIO11, MOSI=GPIO12, CS=GPIO40, DC=GPIO5, RST=GPIO41, TE=GPIO6 |
| Battery sense | GPIO4 through a 3× divider (18650 holder) |
| Buttons | BOOT=GPIO0, KEY=GPIO18 (both active low) |
| Memory | 8 MB octal PSRAM @ 80 MHz, 16 MB QIO flash |

The board exposes a USB-Serial/JTAG console; it resets automatically when `idf.py` opens
the port, so no manual reset is needed to flash.

## Build and flash

ESP-IDF **v6.1** is required. `build.bat` reads `IDF_PATH` and `IDF_TOOLS_PATH` from the
environment and falls back to the standard Windows install locations:

```bat
cd firmware
build.bat build
build.bat -p COM7 -b 921600 flash
build.bat -p COM7 monitor
```

Replace `COM7` with your own port. `idf.py` works directly too, once the ESP-IDF
environment is exported.

The build downloads LVGL 9.5 and cJSON 1.7.19 through the ESP-IDF component manager on
first run, then resolves them from the local cache.

## First-time setup

A fresh board has no credentials, so it raises its own access point:

1. Join the WiFi network **`DeepSeek-Monitor`** (open, no password).
2. Open <http://192.168.4.1/>.
3. Enter a **2.4 GHz** SSID, its password, and your DeepSeek API key.
4. Submit. The board drops the access point, joins your network, syncs the clock over
   SNTP, and takes its first balance reading.

**Hold BOOT for about 3 seconds** at any time to reopen the setup page. Credentials are
stored in the `dscfg` NVS namespace and survive a reflash of the application.

The setup form is also raised automatically whenever the board cannot reach the stored
network — two minutes after it goes offline. Without that, a wrong password is
indistinguishable from dead hardware: the board associates, fails the handshake, retries
forever, and never offers a way back in.

The status bar reports which state the link is in:

| Status bar | Meaning |
|---|---|
| `WIFI:--` | No credentials stored; nothing is being attempted |
| `WIFI:TR` | Credentials stored and a connection is in progress |
| `WIFI:OK` | Connected, address held |
| `WIFI:ER` | The attempt failed; the driver will retry |

Two things about the driver are easy to get wrong here, and both are handled in
`wifi_prov.cpp`:

- **`esp_wifi_set_config()` writes to the driver's RAM, not to NVS.** Storing credentials
  from the form and reconnecting without pushing them into the driver makes the reconnect
  target the *previous* network, so setup appears to fail and then works after a reboot.
  `wifi_apply_stored_credentials()` is what closes that gap.
- **The station must not retry while the access point is up.** The radio is in `APSTA`
  mode, so a station looping on reconnects competes with the access point and the form's
  requests are dropped. `wifi_prov_suspend_station()` freezes it for the duration.

The ESP32-S3 radio is 2.4 GHz only.

## How the numbers are derived

DeepSeek publishes only `GET /user/balance`. There is no spending-history, usage, or
billing endpoint, so `BURN` and `RUNWAY` are computed on the device from the balance
itself.

The ledger keeps two running totals in NVS rather than a window of samples:

```
spent_cf      total CNY spent since monitoring began, in 0.01-fen units
elapsed_s     total seconds monitored
ref_balance   the balance the next delta is measured against
```

Every poll (3 minutes by default, `APP_BALANCE_POLL_MS`) folds in one reading:

```
delta = ref_balance - new_balance
if delta > 0:  spent_cf += delta;  elapsed_s += time since last reading
ref_balance = new_balance
```

A *rising* balance means a top-up or a corrected reading, so the reference simply moves up
and no spend is recorded for that interval. Then:

```
BURN   = spent × 86400 / elapsed        CNY per day
RUNWAY = balance / BURN                 days remaining
```

Because `elapsed` only ever grows, the window is the whole monitored period — this is a
lifetime average, not a recent trend. Both fields show `--` until an hour of history has
accumulated.

**Token estimate**: `BALANCE ÷ current cache-hit input price × 1M`, using the peak price
during peak hours and the off-peak price otherwise. `BASIS` on screen shows that same rate
so the arithmetic is checkable by eye.

Details and the reasoning behind each threshold are in `main/usage_ledger.h`.

### Tariff rules

From the [DeepSeek pricing page](https://api-docs.deepseek.com/zh-cn/quick_start/pricing):

- **Peak** — Monday to Friday, 09:00–12:00 and 14:00–18:00 **Beijing time**, excluding
  Chinese statutory holidays.
- **Off-peak** — everything else, including weekends and holidays all day.
- Off-peak prices are exactly half of the peak prices.

Chinese holidays and compensatory workdays for 2026 are in `main/holidays_2026.cpp`, and
`NEXT` is computed by walking forward to the next state change rather than from a table, so
it stays correct across the weekend gap.

### Keeping the holiday table current

`main/holidays_2026.cpp` only covers the year it was compiled for, so the arrangement for a
later year has to come from somewhere else. Three sources are tried in order, and their
responses are normalised to the same four day kinds (workday / weekend / holiday /
compensatory workday):

| Source | Daily query | Yearly query |
|---|---|---|
| `holiday.dreace.top` | `/<YYYY-MM-DD>`, ~63 bytes | — |
| `timor.tech` | `/api/holiday/info/<date>`, ~200 bytes | `/api/holiday/year/<year>`, ~3 KB |
| `publicapi.xiaoai.me` | `/holiday/day?date=<date>`, ~143 bytes | `/holiday/year?date=<year>`, ~4.5 KB |
| `chinese-days` | — | `cdn.jsdelivr.net/npm/chinese-days/dist/years/<year>.json`, ~1.6 KB |

`dreace` is tried first because the other two query services sit behind Cloudflare and that
path is not reachable from this board — they resolve, then the connection fails. All are kept
anyway, since the reachability of any one host is not something the firmware can assume.
`chinese-days` is a static JSON release rather than a query service, published by an
automated pull request when the State Council publishes; both of its CDN hosts are listed.

Measured on the board, which is why the order is what it is:

```
I holidays: 2026-10-07 is a holiday (via dreace)                     daily, 1 request
W holidays: timor.tech -> 172.67.164.246 but connect failed          Cloudflare
W holidays: publicapi.xiaoai.me -> 104.21.56.121 but connect failed  Cloudflare
I holidays: stored 2026: 33 holidays, 6 makeup days (via chinese-days/jsdelivr)
```

So of the four, `dreace` answers the daily query and `chinese-days` the yearly one. As of
2026-10 all four and the compiled table agree exactly: 33 holidays and 6 compensatory
workdays.

The **daily** query runs once a day and answers only for the day being displayed. That is
what keeps up with an arrangement amended after publication. The **yearly** query runs
monthly and exists because a single-day query says nothing about a future day, and `NEXT`
can fall on one.

Everything is persisted in NVS, and everything falls back to the compiled table, so no
outage can change what the panel shows — it only stops the board from learning:

```
today's cached verdict  ->  cached yearly table  ->  compiled 2026 table
```

`holidays_year_supported()` reports whether either network source covers a given year. When
it returns false the panel is applying plain weekday rules, which means holidays are billed
as peak and compensatory Saturdays as off-peak.

### Refresh cadence and power

| Interval | Setting | Effect |
|---|---|---|
| 3 minutes | `APP_BALANCE_POLL_MS` | API poll, and a ledger update |
| 1 minute | `APP_UI_REFRESH_MS` | Repaint: clock, battery, tariff state |

The poll interval is a power/accuracy trade. Each poll costs a TLS handshake and a radio
wake-up, so a longer interval saves battery; a shorter one means less consumption is lost
when a top-up lands in the same window as spending.

| Interval | Spend lost per top-up | NVS life |
|---|---|---|
| 1 minute | ~0.2 % | ~28 years |
| **3 minutes** | **~0.6 %** | **~38 years** |
| 5 minutes | ~1.0 % | ~143 years |

Three minutes keeps the top-up error under a percent while the radio wakes a fifth as often
as it would at one minute.

Three things matter far more to battery life than that interval:

- **WiFi modem sleep** (`WIFI_PS_MIN_MODEM`, set in `wifi_prov.cpp`). Without it the radio
  stays in the receive chain permanently. The interval decides how often the radio wakes;
  this decides whether it sleeps at all.
- **The control loop sleeps to its next deadline** rather than waking every second to
  compare tick counters. The floor is 250 ms, which is what keeps the BOOT button snappy.
- **The LVGL task's idle period** (`APP_LVGL_IDLE_MS`, 250 ms). `lv_timer_handler()` returns
  0 when nothing is animating, and that used to be floored to 10 ms — 100 wakeups a second,
  about 8.6 million a day, to keep concluding that a static panel had not changed.

Power management is on: `CONFIG_PM_ENABLE` with `CONFIG_PM_DFS_INIT_AUTO`, so the CPU idles
at the XTAL (40 MHz) and runs at an 80 MHz ceiling rather than sitting at a fixed 160 MHz.
Dynamic frequency scaling comes from those options alone, but automatic light sleep does
not: the startup code calls `esp_pm_configure()` with the two frequencies and leaves
`light_sleep_enable` false, so `app_main()` asks for it explicitly.

One consequence is easy to misread. Light sleep does not engage while a USB host is
attached, because the USB-Serial/JTAG connection monitor holds an `ESP_PM_NO_LIGHT_SLEEP`
lock for as long as the console is connected. The `awake=` figure in the heartbeat therefore
reads 100% on a cable, and the saving can only be measured on battery.

### Battery gauge

`battery_percent_from_volts()` in `battery.cpp` maps cell voltage to a percentage. Two
corrections were needed over the mapping in the vendor's ESPHome example, which runs from
2.5 V to 4.2 V in a straight line:

| | Vendor example | This firmware |
|---|---|---|
| Empty | 2.5 V | **3.0 V** |
| Shape | straight line | 18650 discharge curve, interpolated |

Neither 2.5 V nor the ESP32-S3's brownout detector (left at its lowest step, 2.44 V) is what
stops this board. The chip needs a regulated 3.3 V rail and the regulator needs roughly
100–200 mV of headroom, so about **3.0 V is the floor**. With the vendor map the panel read
about **29% at the moment the device died**, which is indistinguishable from a hardware
fault — a board that apparently failed with a third of its charge left.

The straight line is wrong through the middle too. A Li-ion cell sits on a plateau near
3.7–3.9 V for most of its capacity, so a linear map barely moves where the charge actually
goes:

| Cell voltage | Vendor example | This firmware | Error |
|---|---|---|---|
| 4.107 V | 94% | 95% | −1 |
| 3.920 V | 83% | 75% | +8 |
| 3.800 V | 76% | 55% | +21 |
| 3.700 V | 71% | 35% | +36 |
| 3.549 V | 62% | 19% | +43 |
| 3.000 V | 29% | 0% | +29 |

Measured on this board, 4.107 V down to 3.549 V took 71.4 hours, so the gauge moves about
25 points a day on the corrected curve — roughly four days from full. That figure is
approximate in the pessimistic direction: the starting reading was taken with the USB
attached and the charger holding the terminal up, so the true starting point was lower.
Running it down unplugged is the only way to measure the real rate.

The percentage is still only a restatement of the voltage. On the flat part of the curve a
point is worth a large fraction of the remaining runtime, which is why the panel shows the
voltage too when `DEBUG_LOGS` is on.

NVS wear is worked out in `main/usage_ledger.cpp`: at five integer keys per poll and 126
entries per NVS page, the 24 KB partition endures roughly 38 years at a three-minute
interval. Batching those writes would change nothing, because the totals only move when a
poll happens.

## Fonts

The design is set in **Silkscreen**, and the panel uses the same typeface so the layout
measurements hold. The faces are generated with `lv_font_conv` at 1 bpp, and two things
needed care:

- **Proportional, not monospace.** An earlier attempt generated Silkscreen with a fixed
  advance, which made every character as wide as a `W` and pushed the design's 9 px copy
  25–75% past its measured width. The shipping faces keep Silkscreen's own proportional
  metrics, which is what lets the design's font sizes be used as-is.
- **Symbols Silkscreen lacks.** The panel needs `¥`, `■` and `▶`, which Silkscreen does not
  carry. They live in small symbols-only faces (`dsy_*`) that are attached as the
  `fallback` of the matching text face, so LVGL resolves them only when the text face has
  no glyph of its own.

`main/ui_fonts.cpp` builds those fallback chains at startup, because LVGL's font structs
are `const` and cannot be modified in place.

## Source layout

| File | Responsibility |
|---|---|
| `main.cpp` | Startup, power management, the deadline-driven control loop, LVGL mutex, provisioning task |
| `board_rlcd.cpp` | SPI and `esp_lcd` panel IO, ST7305 init, 1bpp frame buffer, LVGL flush |
| `ui.cpp` | Panel layout, tariff-state inversion, all copy |
| `ui_widgets.cpp` | Small widget helpers (boxes, labels, dashed rules) |
| `ui_fonts.cpp` | Fallback chains for the generated faces |
| `pricing.cpp` | Peak/off-peak classification, price table, next-change countdown |
| `pricing_selftest.cpp` | Offline sweep of the tariff rules (development aid) |
| `holidays_2026.cpp` | Statutory holidays and compensatory workdays |
| `clock_time.cpp` | SNTP and the Beijing-time civil clock |
| `deepseek_api.cpp` | `GET /user/balance` over HTTPS with the certificate bundle |
| `usage_ledger.cpp` | Running spend and time totals, burn rate, runway |
| `wifi_prov.cpp` | WiFi station bring-up and the NVS credential store |
| `wifi_portal.cpp` | SoftAP provisioning portal and the BOOT long-press detector |
| `wifi_portal.cpp` | Setup page HTML, form parsing |
| `battery.cpp` | GPIO4 ADC with curve-fitting calibration |
| `app_config_store.cpp` | NVS-backed configuration |
| `frame_dump.cpp` | Frame dump over the console (development aid) |
| `selftest.cpp` | Raw panel bring-up pattern (development aid) |

## Display notes

- Render mode is `PARTIAL` with a 64×32 px staging buffer (4 KB). A full 400×300 RGB565
  buffer would cost 240 KB of PSRAM for no benefit, since the panel keeps its own 15 KB
  1 bpp frame buffer and the whole frame is pushed per flush.
- A full frame push is 15 KB at 10 MHz, roughly 12 ms. The ST7305 has no timing demands.
- The vertical axis is inverted in panel RAM; `board_rlcd.cpp` documents the 2-column ×
  4-row byte packing and the index formula.
- **LVGL is built with `CONFIG_LV_OS_NONE`**, so it has no internal locking. A recursive
  mutex in `main.cpp` serialises the render task and the control loop; without it the two
  corrupt LVGL's object lists and the board wedges.

## Development aids

The switches live in `main/debug_config.h`, in one place so every translation unit
agrees — a `#define` is per unit, and `usage_ledger.cpp` cannot see a macro defined
in `main.cpp`. All are `0` in a release build.

| Switch | Effect |
|---|---|
| `DEBUG_LOGS` | Periodic housekeeping each cycle: link state and countdowns per heartbeat, poll cadence, battery voltage, a line per repaint, ledger totals |
| `HOLIDAYS_FORCE_REFRESH` | Ignore the cached holiday verdict on the next daily check. A cached verdict covers a whole day, so without this a change to the fetch path cannot be observed until tomorrow |
| `FRAME_DUMP_ENABLED` | Streams the real 1 bpp frame over the console so the panel can be inspected pixel-exactly |
| `PRICING_SELFTEST` | Sweeps a week of tariff rules at boot and asserts the expected states |
| `DEMO_DATA` | Renders fixed sample data, for checking the balance paths without a network |
| `BATTERY_REPORT_RAW` | Prints the raw ADC counts and pin voltage for each battery reading |
| `BRINGUP_SELFTEST` | Draws the raw panel bring-up pattern instead of the UI |

`DEBUG_LOGS` deliberately does **not** gate event-driven output. Connection results,
disconnects with their reason, balance fetches, tariff-state changes and every error
still print, which is why the console itself is left enabled rather than disabled: it
is the only diagnostic channel, and turning it off saves nothing.

## Licence

The firmware is provided as-is. LVGL is distributed under the MIT licence; Silkscreen is
distributed under the SIL Open Font License.
