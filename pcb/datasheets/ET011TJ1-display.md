# E Ink ET011TJ1 — 1.1" round 240x240 EPD module (J1, 24-pin FPC)

> Source: `display.pdf` — E Ink Holdings "TECHNICAL SPECIFICATION, MODEL NO: ET011TJ1", Rev 1.0, Oct 17 2017 (p.1–2).
> NOTE ON PART IDENTITY: the PDF is the **E Ink ET011TJ1** datasheet (cover p.1). The pin-assignment table (p.6) names the FPC mating connector as **AXT624124**; the vendor reference-circuit schematic (p.22) instead silk-labels the panel connector footprint **FH35C-23S-0.3SHW(50)** (a 0.3 mm-pitch FPC connector). The pinout below is from the datasheet's own Pin Assignment table (p.6), cross-checked against the reference-circuit netlist (p.22) — both agree pin-for-pin.

## Overview
The ET011TJ1 is a 1.1-inch reflective, bi-stable, active-matrix electrophoretic (E Ink) display module, 240 x 240 pixels, 2 gray levels / 1 bpp, Ø27.96 mm circular active area, on a plastic substrate with plastic back case (p.4). It interfaces over SPI (3-wire or 4-wire) and brings the on-panel driver IC's DC/DC booster pins, gate/source rails, VCOM, an I2C temperature-sensor option, and an optional 2-LED front-light out to a **24-pin 0.3 mm-pitch FPC tail** (connector AXT624124, p.6). On this smartwatch it is J1; the MCU (STM32WB5MMG) drives SCK/SDIN/CS/DC + BUSY + RST_N, and the host PCB must supply the booster's external passives (boost inductor, sense resistor, gate-drive MOSFET, charge-pump caps, Schottky diodes) and decoupling on every rail pin.

## Pinout
FPC connector pin order, from the Pin Assignment table (p.6); "ref-ckt" notes are the recommended external part on that pin from the reference circuit (p.22). Pin 1 is at the VCOM end.

| Pin # | Name | Type | Function | Connect-to / notes |
|------|------|------|----------|--------------------|
| 1 | VCOM | analog (rail) | VCOM driving voltage | Reservoir cap to GND; ref-ckt **C8 = 0.47 µF/25 V** (p.22). Range −2.76 to −1.83 V adjusted (p.8) |
| 2 | VGL | power out (rail) | Negative gate driving voltage | Charge-pump output; ref-ckt **C10 = 1 µF/25 V** to GND, fed via Schottky **D3 (MBR0530)** (p.22) |
| 3 | VDL | power out (rail) | Negative source driving voltage | Reservoir cap to GND (ref-ckt) (p.22) |
| 4 | VGH | power out (rail) | Positive gate driving voltage | Charge-pump output; ref-ckt **C7 = 1 µF/16 V** + **C9 = 1 µF/25 V**, fed via Schottky **D1 (MBR0530)** (p.22) |
| 5 | VDH | power out (rail) | Positive source driving voltage | Reservoir cap to GND; ref-ckt **C4 = 1 µF/16 V** (p.22) |
| 6 | NC | NC | No connect | Leave open (p.6) |
| 7 | VDDDO (VDDIO) | power in | Core-logic / IO power pin ("Core logic power pin") | Decouple to GND; ref-ckt **C3 = 1 µF/16 V** (p.22). Note: standby drops VDDDO to 1.8 V, sleep 1.0 V, deep-sleep 0.0 V — these are *internal* logic states, not what you supply (p.24–26) |
| 8 | VSS | ground | Ground | GND (p.6) |
| 9 | VDD | power in | Power supply (logic) | 2.3 / 3.3 / 3.6 V (min/typ/max, p.8); net "EPD_VDD" in ref-ckt; decouple **C5 = 0.1 µF/6.3 V + C6 = 10 µF/6.3 V** (p.22) |
| 10 | SDA | digital-IO | SPI serial data (SDIN/DIN; bidir DOUT in read) | To MCU MOSI/SDIO (p.6) |
| 11 | SCL | digital-IO | SPI serial clock (SCK) | To MCU SCK (p.6) |
| 12 | CSB | digital-IO (in) | Chip-select input (active low) | To MCU CS (p.6) |
| 13 | DC | digital-IO (in) | Data/Command control pin | To MCU D/C; **used in 4-wire mode** (in 3-wire mode D/C is the first serial bit, p.12) |
| 14 | RST_N | digital-IO (in) | Reset (active low, "RST_N") | To MCU GPIO; hardware-reset releases chip from sleep/deep-sleep (p.29, p.46-glyph) |
| 15 | BUSY_N | digital-IO (out) | Busy-state output ("BUSY_N") | To MCU GPIO input; monitor for update completion (p.6, p.23). Low = busy during PON/refresh/POF (p.15) |
| 16 | BS | digital-IO (in, strap) | Bus selection pin | **L = 4-wire IF, H = 3-wire IF (default)** — see strap table (p.6) |
| 17 | TSDA | digital-IO (I2C) | I2C data to external digital temp sensor | Optional ext temp sensor (Note 5-1, p.6); needs pull-up if used (ref-ckt R6/R7 4.7K, p.22). Else NC |
| 18 | TSCL | digital-IO (I2C) | I2C clock to external digital temp sensor | Optional ext temp sensor (Note 5-1, p.6); needs pull-up if used. Else NC |
| 19 | VSHR | analog (rail) | Positive driving voltage for driver-IC use | Reservoir cap; ref-ckt **C1 = 1 µF/16 V** to GND (p.22) |
| 20 | RESE | analog (sense) | Current-sense input for the control loop | Bottom of boost sense resistor; ref-ckt **R3 = 0.47 Ω** to GND (booster current sense) (p.6, p.22) |
| 21 | GDR | digital-IO (out) | N-channel MOSFET gate-drive control | Drives gate of boost switch FET; ref-ckt **Q1 = Si1308EDL** gate (p.6, p.22) |
| 22 | VSS | ground | Ground | GND (p.6) |
| 23 | FL+ | power (LED) | Front-light LED+ (default NC) | Front-light anode; default not connected (p.6). VLED 5.2–5.6 V (p.14) |
| 24 | FL− | power (LED) | Front-light LED− (default NC) | Front-light cathode; default not connected (p.6) |

## Power & supply
- **Logic supply VDD (pin 9):** min 2.3 V, typ 3.3 V, max 3.6 V (p.8). Absolute-max VDD = −0.3 to +6 V (p.8).
- **VDDDO / VDDIO (pin 7):** core-logic/IO power; internal states are 1.8 V (standby), 1.0 V (sleep), 0.0 V (deep-sleep) (p.24–26). Decouple with C3 = 1 µF/16 V (p.22).
- **Operating temperature:** 0 to +50 °C; storage −25 to +60 °C (p.8). (Commercial range — watch for a smartwatch spec'd colder.)
- **Currents (VDD):** IVDD typ 6.7 mA, max 18 mA at VDD = 3.3 V (p.8). Typical power Ptyp typ 22.2 mW / max 65 mW; standby power Pstby max 0.1 mW (p.8).
- **VCOM (pin 1):** −2.76 V (max-power panel) to −1.83 V (adjusted/typical) (p.8).
- **Decoupling / reservoir caps (from reference circuit, p.22):**
  - VDD (pin 9, EPD_VDD): C5 = 0.1 µF/6.3 V, C6 = 10 µF/6.3 V
  - VDDDO (pin 7): C3 = 1 µF/16 V
  - VSHR (pin 19): C1 = 1 µF/16 V
  - VDH (pin 5): C4 = 1 µF/16 V
  - VGH (pin 4): C7 = 1 µF/16 V (+ C9 = 1 µF/25 V on the D1 charge-pump node)
  - VGL (pin 2): C10 = 1 µF/25 V (on the D3 charge-pump node)
  - VCOM (pin 1): C8 = 0.47 µF/25 V
- **Power sequence (p.15):** On VDD rise, **RST_N is released at ~90 % of VDD**, after which VDD reaches ~95 %; timing markers ~5 ms + 5 ms then ~80 ms before the command sequence `PON → Booster ON → Tsensor → OTP Load → Refresh`. Power-off: `POF → Booster OFF`. BUSY_N is driven low during these operations and must be polled high before issuing the next power command (POF only valid when BUSY_N = 1, p.29).

## Configuration / strap & mode pins
**BS — Bus-select strap (pin 16)** (p.6):

| BS level | Interface |
|----------|-----------|
| L (low) | 4-wire SPI (uses DC pin 13 as data/command) |
| H (high) — **default** | 3-wire SPI (D/C embedded as first serial bit) |

Note: BS is the only hardware interface strap. There is **no I2C address-select strap on the panel itself** — the panel's host bus is SPI. (The TSDA/TSCL pins 17/18 form a *separate* I2C bus to an *optional external* temperature sensor; in the reference circuit that external sensor is a TCN75 with address pins A0/A1/A2 — but that device is external to the panel, not part of J1's strapping.)

Software-side power configuration (not a pin strap, but affects which rail pins are active) — **R01H PWR register** (p.29):
- `VS_EN`: 0 = external source power on VDH/VDL pins; 1 = internal DC/DC generates VDH/VDL.
- `VG_EN`: 0 = external gate power on VGH/VGL pins; 1 = internal DC/DC generates VGH/VGL.
- **R00H PSR** `SHD_N` (booster shutdown), `RST_N` (soft reset → booster off + registers default), `REG_EN` (LUT from OTP vs register), `SHL` (source shift direction) (p.28).

## Interface
- **Bus:** SPI to the panel driver IC; selectable 3-wire or 4-wire via BS pin (p.6, p.10–13).
- **3-wire SPI:** the first transmitted bit is the Data/Command flag, followed by the 8-bit command/data byte (p.12). DC pin (13) is unused in this mode.
- **4-wire SPI:** dedicated DC pin (13) carries data/command; 8-bit bytes on SDA (p.11, p.13).
- **Clock / mode:** Data is presented on SDA and clocked on SCL — single-data-rate, CS-framed (CSB active low). Datasheet gives no explicit CPOL/CPHA label, but waveforms (p.10–11) show data sampled on the rising SCL edge with idle behavior consistent with **SPI mode 0 (CPOL=0, CPHA=0)** — *verify against vendor sample code; not stated as a mode number in datasheet.*
- **Max clock (from AC timing, p.10–11):** SCL serial clock cycle (write) **min 100 ns → ≈10 MHz max write**; SCL serial clock cycle (read) **min 150 ns → ≈6.7 MHz max read**. SCL high/low pulse width: write ≥35 ns, read ≥60 ns. CSB setup 60 ns / hold 65 ns. SDA setup/hold 30 ns. DC setup/hold 30 ns (4-wire). DOUT access time max 50 ns, output disable 15 ns (p.10–11).
- **Power-up protocol detection:** none stated; interface is fixed by the BS strap at power-on (not auto-detected) (p.6).
- **Busy handshake:** after sending image data, poll **BUSY_N** for update completion (p.23); BUSY_N low = busy.

## Application circuit / external components
All values below are from the vendor reference circuit (p.22) unless noted. The driver IC's on-panel DC/DC **booster requires these host-board parts:**

**Boost / gate-drive front end (generates the high rails):**
- **L1 = NR3010T100M (10 µH)** boost inductor, from VDD (EPD_VDD, pin 9) to the switch node (Q1 drain) (p.22).
- **Q1 = Si1308EDL** N-channel MOSFET — gate driven by **GDR (pin 21)**, drain at the L1 switch node, source to the sense resistor (p.22).
- **R3 = 0.47 Ω** current-sense resistor from Q1 source to GND; its top node is the **RESE (pin 20)** current-sense feedback (p.22).
- **C2 = 4.7 µF/25 V** flying/coupling cap in the charge-pump chain (p.22).
- **D1, D2, D3 = MBR0530** Schottky diodes (charge-pump rectifiers feeding the VGH/VGL rails) (p.22).

**Rail reservoir / decoupling caps:** see Power & supply list above (C1, C3, C4, C5, C6, C7, C8, C9, C10).

**Front light (optional, default NC):**
- LEDs on FL+ (pin 23) / FL− (pin 24); 1 series string of 2 LEDs (Note 6-4/6-5, p.14).
- VLED 5.2 V typ / 5.6 V max; ILED 2 mA typ / 20 mA max; PLED 10.4 mW typ / 112 mW max (p.14).
- Reference circuit shows R1 = 10K, R2 = 10K on the front-light power node (p.22).

**Optional external I2C temperature sensor (on TSDA/TSCL, pins 17/18):**
- Reference circuit U3 = **TCN75** (SO); pull-ups R6 = 4.7K, R7 = 4.7K, R8 = 10K; decoupling C13 = 0.1 µF; R9 = NC (p.22). Only needed if using an external sensor instead of the panel's internal one (Note 5-1, p.6).

## Key electrical specs
- **VDD:** 2.3 / 3.3 / 3.6 V (min/typ/max) (p.8); abs-max −0.3 to +6 V (p.8).
- **IVDD:** typ 6.7 mA, max 18 mA @ VDD = 3.3 V (p.8).
- **Standby power:** Pstby max 0.1 mW (p.8). **No separate deep-sleep current figure is given** — deep-sleep is defined by VDDDO = 0 V, booster off, frame buffer & registers not retained (p.26). *(deep-sleep current: not stated in datasheet.)*
- **VCOM:** −2.76 to −1.83 V (p.8).
- **SPI write clock period:** ≥100 ns (≈10 MHz); read ≥150 ns (≈6.7 MHz) (p.10–11).
- **Logic thresholds VIH/VIL:** referenced in AC waveforms (VIH/VIL labels on CSB/SCL, p.10–11) but **no numeric VIH/VIL DC table is provided — not stated in datasheet.** (Drive logic at VDD level, 2.3–3.6 V.)
- **Front light:** VLED 5.2–5.6 V, ILED 2–20 mA (p.14).
- **Refresh / update time:** not stated as a single number; PLL sets frame rate (R30H, p.33-region). *(refresh time: not stated in datasheet.)*
- **Operating temp:** 0 to +50 °C (p.8).

## Schematic-review checklist
- [ ] **BS strap (pin 16):** default-high = 3-wire SPI. If the firmware/driver expects **4-wire SPI (DC pin used)**, BS must be tied **LOW**. Confirm the J1 net for BS matches the intended SPI mode and that the DC pin (13) is wired only if 4-wire (p.6).
- [ ] **Booster external parts present and correct:** L1 ≈10 µH (NR3010T100M), Q1 N-FET (Si1308EDL) with **gate to GDR (pin21)**, drain to L1 switch node, source to **R3 0.47 Ω** sense, top of R3 to **RESE (pin20)**. Missing any of these = no high-voltage rails, panel won't update (p.22).
- [ ] **Schottky diodes D1/D2/D3 (MBR0530)** populated for the VGH/VGL charge pump; flying cap **C2 4.7 µF/25 V** present (p.22).
- [ ] **Every rail pin has its reservoir cap:** VCOM(C8 0.47µF/25V), VGL(C10 1µF/25V), VDL, VGH(C7 1µF/16V + C9 1µF/25V), VDH(C4 1µF/16V), VSHR(C1 1µF/16V), VDDDO(C3 1µF/16V), VDD(C5 0.1µF + C6 10µF/6.3V). Verify voltage ratings — VGH/VGL caps are 25 V parts because gate rails can exceed 16 V (p.22).
- [ ] **VDD ≤ 3.6 V abs-max-adjacent** — typ 3.3 V. Do **not** feed the panel from a 5 V or unregulated rail; confirm it's on the regulated 3.3 V (p.8).
- [ ] **RST_N (pin14)** driven by an MCU GPIO (not tied high) — needs a toggle to wake from sleep/deep-sleep; check release timing relative to VDD ramp (RST_N released ~90 % VDD) (p.15, p.29).
- [ ] **BUSY_N (pin15)** routed to an MCU **input** GPIO (not left floating) — required to poll update completion; check there's no pull that fights the panel's push-pull output (p.23).
- [ ] **CS (CSB pin12) active-low** — ensure default-deasserted (pulled/driven high) at boot so the panel isn't held selected.
- [ ] **TSDA/TSCL (pins 17/18):** if NOT using an external temp sensor, leave NC (the panel has an internal sensor used in the PON sequence). If used, they need their own I2C pull-ups (4.7K) — do not assume the host SPI lines cover them (p.6, p.22).
- [ ] **NC pin 6** left unconnected; **two VSS pins (8 and 22)** both tied to GND; **two VLED front-light pins (23/24)** only populated if a front light is fitted (default NC) (p.6).
- [ ] **Front-light supply:** if FL+/FL− used, provide 5.2–5.6 V with current limiting for ≤20 mA through 2 series LEDs — this is higher than the 3.3 V logic rail, needs its own boost (p.14).
- [ ] **Connector part:** datasheet says mating connector AXT624124 (0.3 mm pitch, 24-pin) — verify J1's chosen FPC connector matches pitch, contact count, and contact side/flip orientation; the ref-ckt footprint was labeled FH35C-23S-0.3SHW (cross-check the actual J1 footprint to the FPC tail) (p.6, p.22).
- [ ] **Operating temp 0–50 °C** — a wrist-worn watch can go below 0 °C; confirm this commercial-range panel is acceptable or de-rate expectations (p.8).
