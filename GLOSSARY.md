# Glossary

The words this project uses, and what they mean *here*. Code and comments should use these
terms and no synonyms: the firmware has one screen, one ledger and one set of rules, and
drifting vocabulary is how a term ends up meaning two things in two files.

## The panel

**Panel** — the ST7305 reflective LCD, 400x300, one bit per pixel. Write-only: it has no
read path, so nothing in the firmware can ask it what it is displaying.

**Frame** — one full 15000-byte bitmap, `W/2 * (H/4)` at this panel's packing. "Pushing a
frame" means sending all of it; there is no partial update.

**Paint** — what `ui_update()` does: set label text and colours on LVGL objects. An
*update* repaints only what changed; a *repaint* happens when `ui_update` runs at all.

**Boot repair** — the single re-send of the panel init sequence a few seconds after the
first paint. It exists because a panel that comes up dark cannot be detected from software.

**View model** — `ui_model_t`. Everything the panel needs for one paint, assembled by
`panel_model.cpp`. The UI reads nothing else.

## Tariff

**Peak** — DeepSeek's published peak rate: Monday to Friday, 09:00-12:00 and 14:00-18:00
Beijing time, on days that are not statutory holidays. Off-peak is exactly half.

**Peak window** — one of the two daily intervals above. A *window* is a time range; a
*period* is not a term here.

**Statutory holiday** — a day the State Council designates as a full day off. Off-peak all
day, even on a weekday.

**Makeup workday** — a weekend or holiday-block day the State Council designates as a
working day. Peak windows apply normally, so it must not be treated as a weekend.

**Arrangement** — the two lists above for one year. Fetched, cached, and fallen back to the
compiled table when unavailable.

## Money

**Balance** — the account's total, from `GET /user/balance`. The only number the API gives.

**Token equivalent** — the balance expressed in tokens, derived from the cache-hit rate.
Shown as `895.83M TOKENS`.

**BASIS** — the per-million rate the token equivalent was derived at. Off-peak, so
`0.06/M` while the panel says `TROUGH`.

**Ledger** — the on-device record of spend: total spent, total monitored seconds, and the
balance the next delta is measured against. Lives in NVS, namespace `dsledger`.

**Monitored window** — the seconds the ledger has actually observed. Excludes time the
device was off or offline, capped at 24 h per interval.

**BURN** — spend per day over the whole monitored window, not over a recent slice.

**RUNWAY** — balance divided by BURN. Days until the account is empty at the current rate.

## Battery

**Cell** — the 18650. Its voltage is what the ADC measures, through a divider.

**Terminal voltage** — the voltage at the cell's terminals right now. Lifted by roughly
200 mV while a charger is connected, which is why it cannot be published directly.

**Resting voltage** — terminal voltage with no charger and no load. What the discharge
curve is calibrated for.

**Published percent** — the number on the panel. Follows the terminal reading except while
a charger is lifting it, when the last resting value is held. See `battery_curve.h`.

**Gauge** — the state machine that decides the above, and therefore what the status bar
says.

**CH / FC / LB** — the three states that replace the number: charging, full, low battery.
`FC` means the terminal has stopped rising at the top, which is about 85% of the cell, not
100%: the constant-voltage taper cannot be seen from the terminal.

## Network and configuration

**Provisioning portal** — the access point and web form used to enter WiFi credentials and
the API key. Raised on first boot, on a BOOT long press, and after a sustained dropout.

**Offline** — the station is not connected but credentials are stored. Distinct from *no
credentials*, which is what decides whether the portal opens immediately.

**Source** — one upstream holiday API, with its URL formats and the parser its payload
needs. Three are configured; order matters because reachability differs.
