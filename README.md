# Encre

From-scratch firmware for the Xteink X4/X3 e-readers (ESP32-C3, 1-bit e-ink).

- `core/`  — portable, hardware-free C++20 (layout, fonts, view-models, themes). Builds on macOS.
- `sim/`   — desktop simulator: renders any screen to PNG at exact panel size.
- `shell/` — Arduino/PlatformIO layer binding core to freeink-sdk drivers.
- `tools/` — offline tools (font converter).
- Spec: `docs/superpowers/specs/2026-08-20-ereader-firmware-v1-design.md`

## Quickstart (desktop)
    make test        # build core + run unit/golden tests
    make sim         # render the Home screen to build/home.png

## Firmware
    pio run -e xteink            # build
    pio run -e xteink -t upload  # flash over USB-C

## Status
Phase 1 (foundation) complete: core builds and tests on macOS, simulator
renders Home, firmware builds for the ESP32-C3. Next: Phase 2 (UI system) —
see docs/superpowers/plans/2026-08-20-v1-roadmap.md.

Flashing to hardware is not yet verified — see the Phase 1 plan, Task 11 Step 9.
