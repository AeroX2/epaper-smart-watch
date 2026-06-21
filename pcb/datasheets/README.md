# Datasheet quick-reference (for schematic review)

Review-focused markdown extracts of the component datasheets in `pcb/`. Each file has the same
sections: **Overview · Pinout · Power & supply · Configuration/strap & mode pins · Interface ·
Application circuit · Key electrical specs · Schematic-review checklist**, with page citations back
to the source PDF. These are aids, not replacements — the PDFs remain authoritative.

| Ref | Part | Role | Extract | Source PDF |
|-----|------|------|---------|------------|
| U4 | STM32WB5MMG | BLE/802.15.4 MCU module (internal antenna) | [STM32WB5MMG.md](STM32WB5MMG.md) | stm32wb5mmg.pdf |
| IC1 | Nordic nPM1100 | USB Li-ion charger + power path + buck | [nPM1100.md](nPM1100.md) | nPM1100_PS_v1_3-3114436.pdf |
| IC3 | MAX17048 | 1-cell fuel gauge (I²C) | [MAX17048.md](MAX17048.md) | max.pdf |
| AC1 | Bosch BMA400 | 3-axis accelerometer (I²C) | [BMA400.md](BMA400.md) | bst-bma400-ds000.pdf |
| U1 | BMP280 / BME280 | Pressure(+humidity) sensor (I²C) | [BME280.md](BME280.md) | bme280.pdf |
| IC2 | Winbond W25Q32JV | 32 Mbit SPI NOR flash | [W25Q32JV.md](W25Q32JV.md) | w25.pdf |
| J1 | ET011TJ1-class panel | 1.1″ round e-paper on 24-pin FPC | [ET011TJ1-display.md](ET011TJ1-display.md) | display.pdf |

## Top gotchas surfaced during extraction
- **STM32WB5MMG**: the per-pin **alternate-function (SPI/I²C/timer) map is NOT in the module
  datasheet** — it lives in the STM32WB55xx datasheet **DS11929** (and RM0434). Use that / CubeMX for
  peripheral pin options. Pin **17 = VDD** (main supply), pin **15 = VBAT**, pin **32 = VDDUSB**;
  ANT_IN(58)/RF_OUT(59) **must be GND** (internal antenna); BOOT0 has an internal pull-down.
- **nPM1100**: datasheet naming is inconsistent — QFN Table 24 calls the straps "VOUTBSET1"(pin2)/
  "VOUTBSET2"(pin3), but functionally they are **VOUTBSET1 (MSB) / VOUTBSET0 (LSB)** (Table 18). Both
  HIGH ⇒ 3.0 V (the max — it's a buck, no 3.3 V).
- **BMA400**: for I²C, **CSB must be hard-wired to VDDIO**; a rising edge on CSB after power-up
  latches SPI permanently (≈120 kΩ internal pull-up on CSB).
- **BME280 vs BMP280**: pin/footprint/address compatible, but BME280 adds humidity. Confirm which
  part U1 should actually be.
- **W25Q32JV**: 2.7–3.6 V (133 MHz needs ≥3.0 V; ~104 MHz at 2.7–3.0 V); the "…Q" part ships with the
  QE bit set.
- **ET011TJ1 display (J1)**: BS pin (16) **L = 4-wire / H = 3-wire (default)**; RST_N = pin 14;
  VDD = pin 9; the on-panel DC/DC needs the external boost FET + sense resistor + Schottky charge
  pumps; FL+/FL− (23/24) are the front-light (VLED ≈ 5.2–5.6 V), default NC.

_Generated from the PDFs on 2026-06-20._
