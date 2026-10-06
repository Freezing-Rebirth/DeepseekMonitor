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

The ESP32-S3 radio is 2.4 GHz only, so a 5 GHz-only network will not work.

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

Every poll (5 minutes by default) folds in one reading:

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

Two things matter far more to battery life than that interval:

- **WiFi modem sleep** (`WIFI_PS_MIN_MODEM`, set in `wifi_prov.cpp`). Without it the radio
  stays in the receive chain permanently. The interval decides how often the radio wakes;
  this decides whether it sleeps at all.
- **The control loop sleeps to its next deadline** rather than waking every second to
  compare tick counters. The floor is 250 ms, which is what keeps the BOOT button snappy.

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
| `main.cpp` | Startup, the 1-second control loop, LVGL mutex, provisioning task |
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

Three compile-time switches at the top of `main/main.cpp`, all `0` in a release build:

| Switch | Effect |
|---|---|
| `FRAME_DUMP_ENABLED` | Streams the real 1 bpp frame over the console so the panel can be inspected pixel-exactly |
| `PRICING_SELFTEST` | Sweeps a week of tariff rules at boot and asserts the expected states |
| `DEMO_DATA` | Renders fixed sample data, for checking the balance paths without a network |
| `BRINGUP_SELFTEST` | Draws the raw panel bring-up pattern instead of the UI |

## Licence

The firmware is provided as-is. LVGL is distributed under the MIT licence; Silkscreen is
distributed under the SIL Open Font License.
