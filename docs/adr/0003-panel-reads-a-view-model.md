# 3. The panel reads a view model and nothing else

Date: 2026-11-05
Status: accepted

## Context

`ui.cpp` painted from a struct, but only partly: for the WiFi column it called
`wifi_link_text()` directly from the paint. That function reads the credential store through
NVS when the link state is unknown, so a flash read could happen on the LVGL thread, on any
repaint. Meanwhile the struct carried six fields that were filled and never read, including
the status line the design used to have.

Two paths to the same fact, one of them with a side effect, and neither file made the other
obvious.

## Decision

The panel is a renderer. `panel_model_build()` composes `ui_model_t` from the subsystems,
and `ui.cpp` includes none of their headers. The model carries precomputed strings where the
UI would otherwise have to ask (`wifi_text`).

A field lives in `ui_model_t` only if `ui_update()` reads it.

## Consequences

- Painting touches LVGL and memory only, so a slow or blocked wireless stack cannot stall a
  repaint.
- The model is a plain struct, so what the panel shows can be asserted without a panel.
- Anything new the panel should show has to be added in one place, `panel_model.cpp`, which
  is also the place that shows what the panel depends on.
