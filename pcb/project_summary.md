# E-Paper Smartwatch — Master Technical Summary & Respin ECO

> **Purpose:** Consolidated single-file reference for the custom BLE e-paper smartwatch. **Part I** (§1–§12) = hardware-debug findings, power/battery analysis, and component selection. **Part II** = the full schematic-review change-list / respin ECO (formerly `pcb/RESPIN_FIXES.md`). Intended to be passed to another LLM or engineer for continuity.
>
> **Numbering:** Part I sections are `§1`–`§12` (each with a topic label). Part II respin fixes are `§1`–`§15` / `#1`–`#13`. Where a cross-reference could be ambiguous, it is written as "Part I §N" or "Part II §N".

---

## 1. Project Overview

- **Repo:** `epaper-smart-watch` — KiCad PCB + PlatformIO firmware (Arduino/C++)
- **Current MCU Module:** STM32WB1MMCH6TR (10×6.5 mm LGA, based on STM32WB15CCY)
- **Display:** E Ink ET011TJ1 — 1.1" round, 240×240, 2-grey-level e-paper
- **IMU:** LSM6DS3 (6-axis accel + gyro, I2C mode)
- **Charger/Regulator:** STNS01PUR (LiPo charger + 3.1V / 100 mA LDO)
- **USB-Serial:** CH340E
- **Battery:** 130 mAh LiPo (target)
- **PCB Fab:** JLCPCB (professionally manufactured, X-ray reports clean)

---

## 2. Hardware Debugging — Why the STM32 Wouldn't Boot

### Symptoms
- VDD on JST connector measured 3.1V (power OK)
- NRST, SWDIO, SWDCLK all at 0V
- USB serial (CH340E) connects to PC fine
- ST-Link SWD cannot connect to the MCU

### Root Cause (Confirmed on Bench)
**C18 (10 µF) couples 5V RTS output from the CH340E directly onto the STM32's non-5V-tolerant NRST pin.**

- The CH340E runs in 5V mode (VCC on raw VBUS). Its RTS pin swings 0–5V.
- C18 capacitively couples these 5V edges onto NRST (abs max ~3.4V).
- Measured 4.6V on NRST — permanently damaged the pin (shorted to ground).
- Additionally, 10 µF × 40 kΩ internal pull-up = 0.4s RC time constant, holding NRST low long enough to defeat ST-Link connect-under-reset.

### Fix (Documented in `pcb/RESPIN_FIXES.md`)
1. Remove C18 entirely
2. Add 100 nF from NRST to GND (per ST spec)
3. Add 10 kΩ pull-up from NRST to 3.1V rail
4. Drop the serial auto-reset (programming is via SWD, not serial)

### Other PCB Issues Found (13 Total)
A full schematic audit was performed; the complete change-list (ECO) is now consolidated into **Part II** of this document (formerly `pcb/RESPIN_FIXES.md`). The top issues at a glance (full 13 + layout items + "already correct" list are in Part II):

| # | Severity | Issue | One-Line Fix |
|---|----------|-------|--------------|
| 1 | 🔴 CRIT | C18 couples 5V RTS → NRST | Rebuild NRST net |
| 2 | 🔴 CRIT | STNS01 NTC pin open → charging never starts | 10k NTC→GND |
| 3 | 🟠 HIGH | CH340E in 5V mode vs 3.1V MCU domain | Run CH340E at 3.3V |
| 4 | 🟠 HIGH | Everything on 100 mA LDO → brownout risk | Re-architect power rail |
| 5 | 🟠 HIGH | Q3 boost-FET gate floats ON when panel absent | 100k GDR→GND pulldown |
| 6 | 🟡 MED | Motor loads MCU LDO rail via emitter-follower | Low-side switch on battery rail |
| 7 | 🟡 MED | LSM6DS3 SDX/SCX/SA0 floating | Tie mode/address pins to VDDIO |

---

## 3. Alternative MCU Comparison — STM32WB1MMC vs BGM220S

The user explored switching to the **Silicon Labs BGM220S** (6×6 mm SiP based on EFR32BG22) as a smaller alternative.

### Feature Comparison

| Feature | STM32WB1MMCH6TR | BGM220S (EFR32BG22) |
|---------|-----------------|---------------------|
| **Package** | 10×6.5 mm LGA module | 6×6×1.0 mm SiP |
| **Core** | Dual: Cortex-M4 @ 64 MHz + Cortex-M0+ @ 32 MHz | Single: Cortex-M33 @ 76.8 MHz |
| **Flash** | 320 KB | Up to 512 KB |
| **RAM** | 48 KB | 32 KB |
| **BLE Version** | 5.4 | 5.2 |
| **Antenna** | Ceramic chip antenna (metal-shielded module) | Molded into substrate |
| **Programming** | ST-Link (SWD) | Segger J-Link (SWD) |
| **Arduino Support** | STM32duino (mature community core) | Silicon Labs Arduino Core (newer, official) |
| **IDE** | STM32CubeIDE / PlatformIO | Simplicity Studio 5 |

### Power Draw Comparison (Typical @ 3.0V, 25°C, DC-DC Enabled)

| Metric | STM32WB1MMC (STM32WB15) | BGM220S (EFR32BG22) | Winner |
|--------|-------------------------|---------------------|--------|
| **Radio RX** | 4.5 mA | 4.2 mA (1 Mbps GFSK) | BGM220S (slight) |
| **Radio TX (0 dBm)** | 5.2 mA | 4.6 mA | BGM220S (11% lower) |
| **Active MCU** | ~33 µA/MHz (Cortex-M4) | ~26 µA/MHz (Cortex-M33) | BGM220S (21% lower) |
| **Deep Sleep (SRAM + RTC)** | ~1.8–2.1 µA (Stop 2, 48 KB RAM) | ~1.40 µA (EM2, 32 KB RAM) | BGM220S |
| **Ultra-Low Sleep (Retained RAM)** | ~610 nA (Standby, 48 KB RAM) | ~1.05 µA (EM3, 8 KB RAM) | STM32WB1MMC |
| **Deepest Sleep (No RAM/RTC)** | ~12 nA (Shutdown) | ~170 nA (EM4 Shutoff) | STM32WB1MMC |
| **Wake-up Time (from Deep Sleep)** | ~5.4 µs (Stop 2) | ~3.0 µs (EM2) | BGM220S |

**Verdict:** BGM220S is better for active workloads (radio + compute). STM32WB is better for ultra-deep sleep modes where the device does almost nothing.

---

## 4. E-Paper Display Specs (ET011TJ1 — from spooky.pdf)

Key electrical characteristics extracted from the datasheet:

| Parameter | Value |
|-----------|-------|
| Screen Size | 1.1" round |
| Resolution | 240×240 pixels |
| VDD Range | 2.3V – 3.6V (typical 3.3V) |
| Active Refresh Current (IVDD) | Typical 6.7 mA, Max 18 mA |
| Typical Refresh Power (Ptyp) | 22.2 mW (Max 65 mW) |
| Standby Power (Pstby) | Max 0.1 mW (~30 µA) |
| Deep Sleep Current | ~2 µA (VDDDO = 0V, all signals tied low) |
| Full Refresh Time | ~2.0 seconds |
| Partial Refresh Time | ~300 ms (estimated for small window) |
| Interface | 4-wire SPI (also supports 3-wire) |
| Operating Temp | 0°C to +50°C |

The display driver is in the firmware library at `code/lib/ET011TJ1/`.

---

## 5. LSM6DS3 Accelerometer Power (for Step/Sleep Tracking)

For health monitoring, only the accelerometer is needed (gyro stays in Power-Down):

| Mode | ODR | Current |
|------|-----|---------|
| Accel Low-Power | 12.5 Hz | 24 µA |
| Accel Low-Power | 26 Hz (recommended for pedometer) | 31 µA |
| Accel Low-Power | 52 Hz | 45 µA |
| Accel Normal | 104 Hz | ~70 µA |
| Accel High-Performance | Any | ~240 µA |
| Power-Down (both sensors) | — | 6 µA |

**Key design points:**
- The LSM6DS3 has a **hardware pedometer** (step counter in registers) — no MCU processing needed.
- It has an **8 KB FIFO** for sleep tracking — MCU can sleep for minutes at a time.
- The floating SDX/SCX/SA0 pins (Part II §7) must be fixed or they can add hundreds of µA of leakage.

---

## 6. Regulator Analysis — LDO vs Buck Converter

### Current Design: STNS01 Internal LDO
- Output: 3.1V / 100 mA max
- Quiescent current: ~6–10 µA
- Efficiency: ~74% at full charge (4.2V → 3.1V), ~78% average
- Problem: 100 mA limit risks brownout under combined BLE TX + EPD refresh + motor

### Recommended: External Buck Converter (e.g., TPS62740, TPS62840, or nPM1100)
- Quiescent current: 60–700 nA (vs 6–10 µA for the LDO)
- Efficiency: ~90–92% across load range
- Current capability: 150–300 mA (eliminates brownout risk)

### Implementation Options
1. **Keep STNS01 for charging + add external buck:** Connect STNS01 SYS pin (raw battery) to buck input. Pull STNS01 /SD low to disable internal LDO (500 nA shutdown). Buck provides 3.0–3.1V system rail.
2. **Replace STNS01 with Nordic nPM1100 PMIC:** Integrates charger + 92%-efficient synchronous buck (700 nA Iq, 150 mA output) in a 2×2 mm package.

---

## 7. BLE Power Impact

BLE operates in a duty-cycled handshake pattern:

| Parameter | Value |
|-----------|-------|
| Connection Event Duration | ~3 ms |
| Current During Event (RX+TX avg) | ~4.5 mA |
| Sleep Between Events | MCU deep sleep current |
| Connection Interval (typical watch) | 1.0–2.0 seconds |

### Average BLE Connection Current
- **1.0s interval:** ~14.9 µA added to baseline
- **2.0s interval:** ~8.2 µA added to baseline

BLE advertising (when not connected) draws more — typically ~50–100 µA depending on advertising interval.

---

## 8. Battery Life Estimates (130 mAh Battery)

### Usage Profile: Wake every 1 minute for screen update, BLE connected (1s interval), pedometer active (26 Hz accel)

#### With Current LDO Regulator

| Component | Continuous Draw |
|-----------|----------------|
| MCU Deep Sleep (Stop 2 / EM2) | ~1.8 µA |
| EPD Deep Sleep | ~2.0 µA |
| LSM6DS3 Pedometer (26 Hz LP) | 31 µA |
| LDO Quiescent | ~8 µA |
| BLE Connection Keepalive (1s) | ~13.5 µA |
| **Subtotal (Sleep Baseline)** | **~56.3 µA** |
| EPD Partial Refresh (300 ms/min) | ~33.5 µA avg |
| MCU Active for SPI + I2C (2 ms/min) | ~0.06 µA avg |
| **Total Average Current** | **~90 µA** |
| **Battery Life (130 mAh)** | **~60 Days (~2 Months)** |

#### With Optimized Buck Regulator

| Component | Continuous Draw |
|-----------|----------------|
| MCU Deep Sleep | ~1.8 µA |
| EPD Deep Sleep | ~2.0 µA |
| LSM6DS3 Pedometer (26 Hz LP) | 31 µA |
| Buck Quiescent | ~0.4 µA |
| BLE Connection Keepalive (1s) | ~13.5 µA |
| **Subtotal (Sleep Baseline)** | **~48.7 µA** |
| EPD Partial Refresh (300 ms/min) | ~31.2 µA avg |
| **Total Average Current** | **~80 µA** |
| **Battery Life (130 mAh)** | **~68 Days (~2.3 Months)** |

#### Without Pedometer (Just Clock + BLE)

| Scenario | LDO | Buck |
|----------|-----|------|
| **Partial Refresh (1 min)** | ~96 Days | ~117 Days |
| **Full Refresh (1 min)** | ~20 Days | ~22 Days |

### Key Optimisation Levers
1. **Partial updates** (only redraw changed digits) — biggest single impact (~4× improvement over full refresh)
2. **Interrupt-driven sleep during EPD refresh** — use BUSY pin as wake interrupt instead of polling
3. **Buck converter** — eliminates regulator quiescent overhead and improves active efficiency
4. **Longer BLE connection interval** (2s instead of 1s) — saves ~7 µA
5. **Schedule full refresh once/hour** only (for ghosting cleanup)

---

## 9. Firmware Architecture Notes

- **Framework:** PlatformIO with Arduino (STM32duino)
- **Entry point:** [`code/src/main.cpp`](file:///C:/Users/James/Documents/git/epaper-smart-watch/code/src/main.cpp)
- **Display driver:** [`code/lib/ET011TJ1/`](file:///C:/Users/James/Documents/git/epaper-smart-watch/code/lib/ET011TJ1/) — custom SPI driver
- **Linker script:** `code/STM32WB1MMCHX_FLASH.ld`
- **Current `waitWhileBusy()` implementation** busy-polls the BUSY pin in a while loop with `delay(10)` — should be replaced with sleep + GPIO interrupt for power savings.
- **SPI Clock:** 4 MHz

---

## 10. Open Questions / Next Steps

1. **MCU Choice:** ✅ RESOLVED — see §11. Verified STM32 vs Nordic vs Si Labs power; shortlisted to two JLCPCB finalists (BGM220S / STM32WB5MMG).
2. **Regulator Selection:** ✅ RESOLVED — see §11.5. Recommend Nordic nPM1100 (compact) or nPM1300 (full-featured) integrated PMIC over discrete TPS62740/840.
3. **PCB Respin:** The 13 fixes in `RESPIN_FIXES.md` need to be applied in KiCad before the next fabrication run.
4. **Firmware:** Deep sleep modes, BUSY pin interrupt, and pedometer integration have not been implemented yet — current firmware just runs test patterns.

---

## 11. Component Selection — MCU, Accelerometer, PMIC (verified 2026-06-17)

All radio/sleep/active numbers below were cross-checked against manufacturer datasheets; module sizes and prices were pulled from LCSC/JLCPCB. Prices are unit "from" prices (low qty) and exclude JLCPCB feeder fees (see §11.7).

### 11.1 Key finding — the MCU barely affects battery life

In the §8 budget the MCU is ~2% of the total (active ~0.06 µA avg, deep sleep ~1.8 µA). The radio/active differences between candidate MCUs wash out. **Pick the MCU on size / antenna / USB / sourcing, NOT on power.** The real battery lever is the accelerometer (§11.3).

**Verified MCU power (typ, 3.0 V, DC-DC/SMPS on):**

| SoC | Active | RX / TX(0dBm) | Deep sleep | Note |
|-----|--------|---------------|-----------|------|
| STM32WB15 (current) | 33 µA/MHz | 4.5 / 5.2 mA | ~1.8 µA | Datasheet-exact; already competitive |
| nRF52832 | ~52 µA/MHz | 5.4 / 5.3 mA | ~1.9 µA | Not lower power than WB15 |
| nRF52840 | ~52 µA/MHz | 4.6 / 4.8 mA | ~1.5 µA | + USB |
| nRF54L15 | ~20 µA/MHz | 3.4 / 4.8 mA | ~1.0 µA | Only part that clearly beats WB15 on power |
| EFR32BG22 (BGM220S) | 27 µA/MHz | 3.6 / 4.1 mA | 1.4 µA (EM2) | Best mature sleep+radio |

*Switching STM32WB15 → nRF52 does NOT save power. Only nRF54L15 / BG22 meaningfully improve it.*

### 11.2 Optimization levers, ranked by impact (130 mAh, LDO base ≈ 60 d)

| # | Change | Approx saving | Battery |
|---|--------|---------------|---------|
| 1 | **Accelerometer LSM6DS3 → BMA400** | **~27 µA** | → ~86 d |
| 2 | Buck (vs LDO) | ~7–12 µA | → 68–73 d |
| 3 | Accel ODR 26 → 12.5 Hz (if keeping LSM6DS3) | ~7 µA | → ~64 d |
| 4 | BLE interval 1 s → 2 s | ~7 µA | → ~64 d |
| 5 | MCU swap (STM32 → any Nordic) | ~0–1 µA | ~unchanged |

### 11.3 Accelerometer — biggest lever (all Bosch wearable parts are 2.0 × 2.0 mm)

The LSM6DS3 (~31 µA) is a 6-axis part run accel-only (gyro wasted) and is the single largest always-on load. Dedicated ultra-low-power 3-axis accels with a hardware step counter cut this dramatically at the *same* footprint (~4 mm² chip, ~7 mm² with 2 decoupling caps).

| Part | Step-counter current | LCSC $ | LCSC # | Note |
|------|---------------------|--------|--------|------|
| **BMA400** (recommended) | **~4 µA** | $2.13 | C437655 | Lowest power |
| BMA456 | higher | $0.98 | C189518 | Wearable features |
| BMA421 | higher | $0.42 | C5242966 | In PineTime; cheapest |

### 11.4 BLE module shortlist (all integrated-antenna + on JLCPCB)

Sorted by board area (the binding constraint). Cheap modules use PCB-trace antennas → physically larger; ESP32 parts also have worse sleep. Small = SiP/chip-antenna modules.

| Module | SoC | Size | Area | USB | Sleep | JLCPCB $ | LCSC/JLC # |
|--------|-----|------|------|-----|-------|----------|-----------|
| **BGM220S** | EFR32BG22 | 6 × 6 | **36 mm²** | ❌ | 1.4 µA | $5.96 | C1019217 |
| ANNA-B112 | nRF52832 | 6.5 × 6.5 | 42 mm² | ❌ | ~1.9 µA | $11.89 ⚠️2 stk | C5451782 |
| STM32WB1MMC (current) | WB15 | 6.5 × 10 | 65 mm² | ❌ | 1.8 µA | ~$5–6 | — |
| **STM32WB5MMG** | WB55 | 7.3 × 11 | 80 mm² | ✅ | 1.8 µA | $8.87 | C5184586 |
| NINA-B306 | nRF52840 | 10 × 15 | 150 mm² | ✅ | ~1.5 µA | $13.11 ⚠️8 stk | C6127317 |
| Ai-Thinker PB-03 | PHY6252 | 13.2 × 16.6 | 219 mm² | ❌ | ~1 µA | $0.67 | C2980079 |
| ESP32-H2-MINI-1 | ESP32-H2 | 13.2 × 16.6 | 219 mm² | ✅ | ~7 µA | $4.27 | C18202993 |
| ESP32-C3-MINI-1 | ESP32-C3 | 13.2 × 16.6 | 219 mm² | ✅ | ~5 µA | $3.09 | C2838502 |
| Ebyte E73-2G4M04S1B | nRF52832 | 17.5 × 28.7 | 502 mm² | ❌ | ~1.9 µA | $4.59 | C411306 |

**Two finalists (the only decision left is: do you need USB on-board?):**
- **BGM220S** — smallest (36 mm²), lowest sleep (1.4 µA), cheapest, best stock. No USB → program via SWD.
- **STM32WB5MMG** — smallest module *with native USB* (80 mm², $8.87) → lets you delete the CH340E; keeps ST tooling.

*u-blox ANNA/NINA are sourceable but premium-priced, low-stock, and not smaller — not worth it here.*

### 11.5 PMIC — one chip for all power

| PMIC | Package | ≈area w/ passives | Rails | LCSC / JLCPCB | # |
|------|---------|-------------------|-------|---------------|---|
| **nPM1100** | 2.1×2.1 WLCSP | ~15 mm² | 1× 150 mA buck + charger | $0.91 | C7249859 |
| **nPM1300** | 2.75×3.5 QFN | ~28 mm² | 2× 200 mA buck + 2 LDO + charger + fuel gauge | $4.62 / $2.72 | C7501206 |

nPM1100 = smallest/cheapest (keep haptic motor on its own low-side switch off the battery, per RESPIN #6). nPM1300 = bigger but gives a dedicated motor rail + fuel gauge.

### 11.6 Recommended stacks (module + PMIC + accelerometer)

| Stack | Parts | Active area* | Parts cost | USB |
|-------|-------|--------------|-----------|-----|
| **C — smallest/cheapest** | BGM220S + nPM1100 + BMA400 | ~66 mm² | ~$9.00 | ❌ (SWD) |
| C-budget | BGM220S + nPM1100 + BMA421 | ~66 mm² | ~$7.29 | ❌ |
| **A — USB, ST ecosystem** | STM32WB5MMG + nPM1100 + BMA400 | ~112 mm² | ~$11.91 | ✅ |
| A-full | STM32WB5MMG + nPM1300 + BMA400 | ~125 mm² | ~$13.72 | ✅ |

*\*Silicon footprint only (module+PMIC+accel+passives); excludes display, battery, connectors. Current 4-chip path ≈ 120 mm² — Stack C nearly halves it.*

### 11.7 New architecture resolves ~4 of the 13 RESPIN issues

- **Native-USB module (WB5MMG or any nRF52840) → delete CH340E** → fixes **#1** (C18 5V→NRST) and **#3** (CH340E 5V domain). This was the root cause of the board not booting.
- **nPM1100/nPM1300 replaces STNS01** → fixes **#2** (open NTC) and **#4** (100 mA brownout). nPM1300's 2nd rail also addresses **#6** (motor on MCU rail).

### 11.8 JLCPCB sourcing note

All parts above are **"Extended"** on JLCPCB assembly → one-time **~$3 feeder fee per part per order** (negligible at volume, dominant at qty 1–5). Budget total ≈ (parts × qty) + ~$3 × distinct parts + PCB/assembly setup.

---

## 12. Corrections from datasheet verification (2026-06-17)

Minor issues found while verifying the original numbers — directionally everything held, but for accuracy:

- **§5 (accelerometer):** Only **24 µA @ 12.5 Hz LP**, 70 µA @ 104 Hz, 240 µA HP, 6 µA power-down are datasheet values (LSM6DS3 Table 4, **@ Vdd = 1.8 V**). The **26 Hz = 31 µA** and **52 Hz = 45 µA** rows are *interpolations*, not specced. The hardware pedometer adds negligible current (datasheet-confirmed). → Running the accel at **12.5 Hz gives a guaranteed 24 µA** (saves ~7 µA vs the assumed 31).
- **§7 (BLE avg):** Pure duty-cycle math gives **13.5 µA @ 1 s / 6.75 µA @ 2 s**; the table's 14.9 / 8.2 µA carry a ~1.4 µA offset. §8 already uses the correct 13.5 µA.
- **§8 (buck):** LDO 1:1 charge accounting is correct (mAh is charge-based). The buck efficiency factor was applied only to the EPD line; applying it to all loads pushes the buck case to **~72 d (not 68 d)** — i.e. the buck is slightly *understated*. "Without pedometer" figures are ~4–5% optimistic (LDO ~92 d not 96; buck ~111 d not 117).
- **§3 (BGM220S):** Datasheet is slightly *better* than the doc's column (RX 3.6 / TX 4.1 mA vs 4.2 / 4.6).

---

# PART II — Schematic Review & Respin ECO

*Consolidated from the former `pcb/RESPIN_FIXES.md`. **Source of truth:** `pcb/_review/netlist_full.txt` (64 components, 78 nets), generated from `epaper-smart-watch.kicad_sch`, cross-checked against DS14096 (STM32WB1MMC), the STNS01PUR, CH340E, and LSM6DS3 datasheets, and live bench measurements. This is a **change list (ECO)** for the next board revision — it does **not** modify the `.kicad_sch` (graphical placement of new parts must be done in KiCad). Within Part II, all `§N`/`#N` references point to the fix sections below.*

## TL;DR — why the boards don't work

Three independent root causes, plus a power-architecture weakness:

1. **The reset network injects 5 V onto NRST and is grossly mistuned.** `C18` (10 µF) couples the
   CH340E's **5 V** RTS output straight onto the MCU's **non‑5 V‑tolerant** NRST pin with no series
   resistor, no clamp, and no external pull‑up. This over‑stresses NRST (killed at least one module,
   measured 4.6 V on NRST on the bench) **and** the 10 µF + ~40 kΩ internal pull‑up makes a ~0.4 s
   reset that holds the part down and defeats ST‑Link connect‑under‑reset. **This is the primary
   "won't boot / won't connect" cause.**
2. **The charger can never charge the intended 2‑wire LiPo.** The STNS01 `NTC` pin is left open, which
   the chip reads as "battery too cold" → charging suspended → the charge LED (D2) never lights.
3. **The CH340E runs in 5 V mode against a 3.1 V MCU domain** — the structural cause of #1 and of
   back‑power sneak paths.
4. **The whole system (MCU + BLE radio + EPD boost + motor) runs off one STNS01 3.1 V / 100 mA LDO**
   while the dedicated SYS tap sits unused → brown‑out risk under load peaks.

---

## Priority fix table

| # | Sev | Area | Issue | Fix (one line) |
|---|-----|------|-------|----------------|
| 1 | 🔴 CRIT | Reset | C18 couples 5 V RTS → NRST; 10 µF + no pull‑up | Rebuild NRST net (see §1) |
| 2 | 🔴 CRIT | Charge | NTC pin open → no charging | 10 k NTC→GND (see §2) |
| 3 | 🟠 HIGH | USB | CH340E in 5 V mode vs 3.1 V MCU | Run CH340E at 3.3 V (see §3) |
| 4 | 🟠 HIGH | Power | Everything on 100 mA LDO → brown‑out | Re‑architect rail / current budget (see §4) |
| 5 | 🟠 HIGH | EPD | Q3 boost‑FET gate floats ON when panel absent | 100 k GDR→GND pulldown (see §5) |
| 6 | 🟡 MED | Motor | Motor loads MCU LDO rail via emitter‑follower | Low‑side switch on battery rail (see §6) |
| 7 | 🟡 MED | IMU | LSM6DS3 SDX/SCX/SA0 floating | Tie mode/address pins (see §7) |
| 8 | 🟡 MED | USB | MCU PA10 back‑powers CH340 when USB off | Fixed by §3 + FW tristate (see §8) |
| 9 | 🟢 LOW | Power | STNS01 SD on bare 500 k internal pulldown | Tie /SD→GND via 10 k (see §9) |
| 10 | 🟢 LOW | Power | VDDA shares unfiltered rail w/ boost+motor | Ferrite/RC to VDDA (see §10) |
| 11 | 🟢 LOW | Input | VBUS has only ESD TVS, no series OVP/reverse | Optional load‑switch + 6 V TVS (see §11) |
| 12 | 🟢 LOW | BOM | USB‑C symbol says "Plug", wired as receptacle | Swap symbol (see §12) |
| 13 | 🟢 LOW | Charge | ISET = 2 k+2 k = 4 k ≈ 50 mA | Confirm intent / simplify (see §13) |

Layout‑only action items are in §14. **Things that are already correct — do NOT change** are in §15
(read this one — it includes a change that would *break charging*).

> **Note (cross-ref to Part I §11.7):** the recommended module + PMIC architecture resolves several of these outright — a native-USB module deletes the CH340E (fixes **#1**, **#3**, **#8**), and an integrated PMIC (nPM1100/nPM1300) replaces the STNS01 (fixes **#2**, **#4**, and with nPM1300's 2nd rail, **#6**).

---

## §1 🔴 CRITICAL — Rebuild the NRST reset network

**Components:** C18 (10 µF), U2 pin 23 (NRST), U1 pin 4 (RTS), SWD1 pin 2.
**Net today:** `/NRST = C18.2, SWD1.2, U2.23(NRST)`  and  `C18.1 → Net-(U1-~{RTS}) → U1.4 (RTS)`.

**Problem:** Two compounding defects on one node.
- C18 capacitively couples the CH340E's **5 V** RTS edges onto NRST. NRST is a dedicated reset pin,
  **not** 5 V‑tolerant (abs‑max ≈ VDD+0.3 ≈ 3.4 V). With no series resistor, each RTS edge dumps
  uncontrolled clamp current into the pin — this is what put 4.6 V on NRST and killed a module.
- C18 = 10 µF (ST spec is 100 nF) and there is **no external NRST pull‑up**. RC = 40 kΩ × 10 µF ≈
  **0.4 s**, so reset is held/slow → power‑on boot is unreliable and ST‑Link connect‑under‑reset fails.

**Fix:**
1. **Remove C18 from the RTS→NRST path** entirely.
2. Add **C_NRST = 100 nF from NRST to GND**, placed at U2.
3. Add **R_NRST = 10 kΩ from NRST to the 3.1 V rail (/EPD_VDD)**.
4. **Drop the serial auto‑reset** — you program over SWD, so you don't need RTS→NRST at all. (If you
   ever want it: drive NRST **low‑only** through a 3.1 V‑referenced NPN/N‑FET — base/gate from RTS via
   1 k + 100 nF — never a passive cap from a 5 V line.)

Result: NRST sits cleanly at 3.1 V, no 5 V can reach it, and SWD reset is low‑impedance.
*(Bench‑proven: cutting C18 made a module boot and SWDIO go to 3.3 V.)*

---

## §2 🔴 CRITICAL — Bias the STNS01 NTC pin

**Components:** PS1 pin 8 (NTC), J3 pin 3.   **Net:** `Net-(J3-Pin_3) = J3.3, PS1.8` (only members).

**Problem:** The STNS01 sources 50 µA into NTC and only charges when 0.246 V < V_NTC < 1.355 V. With a
bare 2‑wire LiPo the NTC pin is **open** → 50 µA drives it to the internal clamp (~3 V) → read as
"too cold" → **charging permanently suspended**, and the CHG flag never asserts so **D2 never lights**
(your exact symptom).

**Fix:** Populate **R_NTC = 10 kΩ from NTC (PS1.8 / Net‑(J3‑Pin_3)) to GND** → 50 µA × 10 k = 0.5 V,
mid‑band. (10 k also matches a real NTC's R25.) Anything ~6.8 k–20 k works.
*Alternative:* fit a 3‑wire battery with a 10 kΩ B=3370 thermistor on J3 pin 3. **Do not leave NTC open.**

---

## §3 🟠 HIGH — Run the CH340E at 3.3 V, not 5 V

**Components:** U1 (CH340E) VCC pin 7 → /VBUS (5 V), V3 pin 10 + C17, R15/R16.

**Problem:** U1.VCC is on raw 5 V VBUS and V3 is decoupled to GND (5 V‑mode config), so every CH340
output swings 0–5 V into the 3.1 V MCU domain. This is the structural cause of the NRST damage (§1)
and the back‑power path (§8). (The UART pins PA9/PA10 are 5 V‑tolerant so they survive in normal
operation — but only while the 3.1 V rail is up.)

**Fix:**
- Add a small **3.3 V LDO off VBUS** (a few mA is plenty) and power **U1.VCC from 3.3 V**.
- **Tie V3 (pin 10) directly to VCC** (CH340E 3.3 V‑mode config); C17 then sits on VCC.
- Keep R15/R16 = 1 k series on TXD/RXD.
- Note: the 3.1 V system rail is *marginal* for CH340 3.3 V mode (needs ≥ 3.3 V) — use the dedicated
  3.3 V LDO, **not** /EPD_VDD.

Now RTS/TXD/RXD swing 0–3.3 V; combined with §1 the reset/UART interface is fully safe.

---

## §4 🟠 HIGH — Don't run the whole system off the 100 mA LDO

**Components:** PS1 LDO (pin 3, 3.1 V/100 mA) vs SYS (pin 2, only C22). `/EPD_VDD` feeds U2
(MCU+radio), U3 (IMU), J1 (EPD VDD), L1 (EPD boost), Q1 (motor), LEDs.

**Problem:** Every dynamic load hangs on the single 3.1 V / **100 mA** LDO output. Worst‑case
BLE‑TX + EPD‑refresh + motor can approach/exceed 100 mA and sag the rail below the STM32WB BOR
threshold → brown‑out resets ("intermittently won't boot"). The dedicated SYS tap is unused.

**Fix:**
- Budget worst‑case current (BLE TX peak + EPD refresh + motor). If peak > ~80 mA the 100 mA LDO is
  non‑viable as the sole rail.
- Add a dedicated supply (small buck/LDO) sized for the radio peak off **BAT**, and reserve the
  STNS01 LDO for low‑current/analog. Move heavy switching loads (EPD boost L1, motor) off the MCU rail.
- Firmware: sequence EPD refresh / BLE TX / motor so peaks don't overlap.
- ⚠️ Do **not** simply reroute heavy loads onto the SYS pin assuming it's "~1 A" — SYS is **not** rated
  ~1 A; size any new rail deliberately.

---

## §5 🟠 HIGH — Add a gate pulldown on the EPD boost FET (Q3)

**Components:** Q3 (Si1308EDL), gate net `/GDR = J1.21, Q3.1(G)` (only members), L1, R6.

**Problem:** Q3's gate is driven only by the EPD panel's gate driver via J1 pin 21. With the panel
unplugged/unpopulated, or before the panel controller initializes, the gate **floats** → the boost
FET can self‑bias partially ON → shoot‑through L1/Q3 draws uncontrolled current from the already‑tight
3.1 V rail and **overheats Q3** (matches your "Q3 got hot on first plug‑in").

**Fix:** Add **R_GDR = 100 kΩ (100 k–1 M) from /GDR to GND**, physically at Q3's gate, so the FET is
guaranteed OFF whenever the panel isn't actively driving it.

---

## §6 🟡 MEDIUM — Fix the vibration‑motor drive

**Components:** Q1 (MMBT3904), M1 (motor), D9 (flyback), C23. `/VIB = Q1.1(B), U2.32(PA11)`,
`Q1.C → /EPD_VDD`, `Q1.E → Net‑(D9‑K) → motor+`.

**Problem:** The motor is an **emitter‑follower off /EPD_VDD** — i.e. it pulls motor current straight
from the MCU's 100 mA LDO rail (the §4 brown‑out aggressor), and delivers only V_rail − V_be ≈ 2.4 V
to the motor. The base `/VIB` also has **no series resistor** (less harmful in this follower topology
than in common‑emitter, but still poor practice).
*(Note: D9 IS a correctly‑oriented freewheel diode for this topology — keep it; re‑orient only if you
change the drive.)*

**Fix:** Re‑topologize as a **low‑side switch on the battery rail**: NMOS (or NPN common‑emitter) with
the **motor between BAT+ and the drain/collector**, **gate/base via 1 k from /VIB** plus a 100 k
gate/base pulldown, and the flyback diode anti‑parallel across the motor to BAT+. This keeps motor
current off the MCU rail and gives the motor full voltage.
*Minimum change if keeping the topology:* add a ~1 k base resistor on /VIB and accept the rail loading.

---

## §7 🟡 MEDIUM — Tie the LSM6DS3 mode/address pins (it's in I2C mode)

**Components:** U3 pins 1 (SDO/SA0), 2 (SDX), 3 (SCX), 12 (CS). SCL/SDA have I2C pull‑ups R2/R7 to
/EPD_VDD; CS → /Accel_CS (PA5). **The part is wired for I2C** (pull‑ups + I2C nets + STM32 I2C1 pins);
CS on a GPIO held high by firmware is the standard way to keep it in I2C mode.

**Problem:** SDX (pin 2) and SCX (pin 3) are **floating** — in I2C/"mode 1" the datasheet requires
them tied to VDDIO (undefined/excess current otherwise). SA0 (pin 1) floating → **undefined I2C
address**. CS has no strap, so I2C mode depends entirely on firmware driving PA5 high.

**Fix:**
- Tie **SDX (U3.2) and SCX (U3.3) to VDDIO (/EPD_VDD)** per the LSM6DS3 mode‑1 connection.
- Tie **SA0 (U3.1) to GND or VDDIO** to fix the I2C address (pick to match your firmware; GND = 0x6A).
- Optional robustness: add a pull‑up on **CS (U3.12) to VDDIO** so it defaults to I2C even before
  firmware runs.

---

## §8 🟡 MEDIUM — MCU back‑powers the CH340 when USB is unplugged

**Components:** U2 PA10 → R16 (1 k) → JP2 → U1.9 (RXD). **Problem:** on battery (USB out, VBUS = 0),
if firmware drives /RXD high at 3.1 V it forward‑biases the CH340's input clamp (abs‑max VCC+0.5 = 0.5 V
with VCC = 0) and trickle‑powers the dead chip (~2.6 mA battery sneak‑drain).
**Fix:** §3 (CH340 at 3.3 V from VBUS) makes VCC collapse with VBUS, eliminating this. Firmware backstop:
tristate PA10 (input/analog) whenever VBUS is absent.

---

## §9 🟢 LOW — Tie the STNS01 SD (shutdown) pin

`/SD = PS1.4` only (relies on the internal 500 k pulldown). Safe default, but a 500 k high‑Z node can be
coupled high by noise → asserts shutdown when VIN is invalid (= normal on‑battery operation) → cuts the
rail. **Fix:** tie /SD to GND via 0 Ω/10 k, **or** route to a spare GPIO with a 100 k pulldown.

## §10 🟢 LOW — Dedicated VDDA filter

VDDA/VBAT/VDDSMPS (U2 16/18/20) share the rail with the EPD boost and motor with no filter. **Fix:** add
a ferrite bead (or small series R) from /EPD_VDD to U2.VDDA with local 100 nF, to keep the RF/analog
supply quiet. (Tying the three supplies together is ST's reference config — that part is correct.)

## §11 🟢 LOW — VBUS input protection

VBUS → PS1.IN + U1.VCC with only an ESD TVS (D4, 5.0 V standoff), no series element. USB‑C is keyed so
reverse insertion is unlikely; this is field‑hardening. **Fix (optional):** series ideal‑diode/load‑switch
with OVP, or a TVS rated 6.0–6.8 V (above 5 V nominal, below the 10 V abs‑max).

## §12 🟢 LOW — USB‑C symbol/BOM hygiene

P1 symbol is `USB_C_Plug_USB2.0` but the footprint is a receptacle and the 2× 5.1 k Rd are correct sink
termination — it works. **Fix:** swap the schematic symbol to a USB‑C *receptacle* so symbol/BOM match
the footprint. No electrical change.

## §13 🟢 LOW — Confirm charge current

ISET = R1 (2 k) + R9 (2 k) = 4 k → I_fast ≈ 50 mA (valid). The 2 k+2 k series pair is unusual. **Fix:**
confirm 50 mA is intended; if so optionally simplify to a single resistor (a single 1–2 k gives
200–100 mA if you want faster charge for a larger cell).

---

## §14 Layout (not schematic) action items
- Place at least one **100 nF directly at U2 pins 16/18/20**, short returns to the VSS pads (15/17/19/21).
- Place the **2.7 pF caps (C19/C20/C21)** right at PB0/PB1/PB2 and border the tracks with ground plane.
- Maintain the **RF ground keepout** under the antenna edge of the module.
- Place the §10 VDDA ferrite + cap right at the module.

---

## §15 ✅ Already correct — do NOT "fix" these
The audit's adversarial pass flagged several plausible‑sounding "fixes" that are wrong. Leave these alone:

- **CEN (PS1.6) floating — CORRECT.** Internal 500 k pull‑up = charging enabled. ⚠️ **Tying CEN to GND
  would DISABLE charging.** Leave it floating.
- **BOOT0 (U2.22) floating — CORRECT.** The WB1MMC *module* has an internal 10 k pulldown → boots from
  flash. An external pulldown is harmless but **not required**.
- **D9 motor flyback — CORRECTLY oriented** for the emitter‑follower topology. (Re‑check only if you
  adopt the §6 low‑side topology.)
- **C17 = 0.1 µF on V3 — CORRECT** for CH340E 5 V mode. (If you move to §3's 3.3 V mode, tie V3 to VCC
  instead.)
- **D2 charge LED off /EPD_VDD via R5 = 600 Ω — CORRECT** (matches STNS01 reference; LDO is on whenever
  charging). The LED not lighting is the NTC issue (§2), not the LED circuit.
- **CC1/CC2 5.1 k Rd (R3/R4) — CORRECT** sink termination.
- **PB0/1/2 2.7 pF caps — CORRECT** per DS14096 (sensitive GPIOs). Keep.
- **ANT_IN/ANT_OUT shorted — CORRECT** internal‑antenna config.
- **BAT/BATSNS joined at the connector — CORRECT** per STNS01 reference.
- **CH340 CTS floating — CORRECT** (internal pull‑up; flow control off by default).
- **PA9/PA10 UART — 5 V‑tolerant**, safe in normal operation (the only residual is the rail‑off
  back‑power case, fixed by §3).

---

## Minimal "just make it work" subset
If you only do the cheap, high‑impact changes before a respin:
1. **§1** — rebuild NRST (remove C18, add 100 nF→GND + 10 k pull‑up, drop RTS auto‑reset). *Fixes
   boot/SWD and stops killing modules.*
2. **§2** — 10 k on NTC→GND. *Makes charging work.*
3. **§5** — 100 k GDR→GND. *Stops Q3 overheating with no panel.*

These three are bodge‑wireable on the existing boards to validate before committing the respin.
