# STM32WB5MMG (U4) — BLE/802.15.4 wireless MCU module

Datasheet: ST DS13252 Rev 8. This module datasheet covers ordering/mechanical/pin data only; electrical, RF and per-pin alternate-function (peripheral) detail live in the STM32WB55xx datasheet **DS11929** and reference manual **RM0434**. Errata: ES0525.

## Overview
The STM32WB5MMG is an ultra-low-power, certified 2.4 GHz wireless **module** (SiP-LGA86, 7.3 × 11 × 1.382 mm, 86 pads) built around the STM32WB55VGY MCU (dual-core Arm Cortex-M4 + M0+, 1 MB flash, 256 KB SRAM) with an **integrated chip antenna**, integrated 32 MHz HSE and 32.774 kHz LSE crystals, SMPS passives, IPD and antenna matching, and power-supply filtering all inside the package (p.0, p.3, p.4). On this board it is the main BLE MCU (U4) talking to the BMA400, BMP280/BME280, MAX17048, W25Q32 flash and the e-paper FPC. Because the antenna is internal, **ANT_IN/RF_OUT must be tied to GND** and no external antenna is permitted (p.5, p.10).

## Pinout
Full module pin/ball table (Table 1, pins 1–86) in pin-number order. "STM32WB55VGY" column is the underlying die ball for reference. Pin type per datasheet: S = supply, I/O = digital I/O, RST = reset. PB0 (44) and PB1 (43) exist only on revision X (Version X); on Version Y those pads are NC (p.7–9). PH3-BOOT0 (13) has an internal pull-down. Sensitive GPIOs PB10/PB11/PC5 need a 3.3 pF cap close to the pin when used (p.13).

| Pin # | Name | Type | Function | Connect-to / notes |
|------:|------|------|----------|--------------------|
| 1 | PA2 | digital-IO | GPIO (PA2) | per design; AF in DS11929 |
| 2 | PA1 | digital-IO | GPIO (PA1) | per design |
| 3 | PA0 | digital-IO | GPIO (PA0) | per design |
| 4 | VREF+ | power (analog ref) | ADC/analog positive reference | tie to VDDA (or filtered ref); decouple |
| 5 | VSS | ground | Ground | GND |
| 6 | VDDA | power (analog) | Analog supply | tie to VDD via filter; decouple |
| 7 | PC3 | digital-IO | GPIO (PC3) | per design |
| 8 | PC2 | digital-IO | GPIO (PC2) | per design |
| 9 | PC1 | digital-IO | GPIO (PC1) | per design |
| 10 | NRST | RST | Reset (active low) | internal pull-up present; add **100 nF** filter cap to GND (p.10) |
| 11 | PB9 | digital-IO | GPIO (PB9) | per design |
| 12 | PC0 | digital-IO | GPIO (PC0) | per design |
| 13 | PH3-BOOT0 | digital-IO | BOOT0 / PH3 | **internal pull-down** → boots from flash; tie high w/ low-R pull-up only if boot-mode override needed (p.10) |
| 14 | PB8 | digital-IO | GPIO (PB8) | per design |
| 15 | VBAT | power | Backup/main battery supply | connect to VDD/battery rail; decouple |
| 16 | VSS | ground | Ground | GND |
| 17 | VDD | power | Main digital supply (1.71–3.6 V) | supply + decoupling |
| 18 | PB7 | digital-IO | GPIO (PB7) | per design |
| 19 | PB5 | digital-IO | GPIO (PB5) | per design |
| 20 | PB4 | digital-IO | GPIO (PB4) | per design (default NJTRST) |
| 21 | PB3 | digital-IO | GPIO (PB3) | per design (default JTDO/TRACESWO) |
| 22 | PC10 | digital-IO | GPIO (PC10) | per design |
| 23 | PC11 | digital-IO | GPIO (PC11) | per design |
| 24 | PC12 | digital-IO | GPIO (PC12) | per design |
| 25 | PA13 | digital-IO | GPIO (PA13) | per design (default SWDIO) |
| 26 | PA14 | digital-IO | GPIO (PA14) | per design (default SWCLK) |
| 27 | PA15 | digital-IO | GPIO (PA15) | per design (default JTDI) |
| 28 | PA10 | digital-IO | GPIO (PA10) | per design |
| 29 | PA12 | digital-IO | GPIO (PA12) | per design (USB DP capable) |
| 30 | PA11 | digital-IO | GPIO (PA11) | per design (USB DM capable) |
| 31 | VSS | ground | Ground | GND |
| 32 | VDDUSB | power | USB transceiver supply | tie to USB supply if USB used; else tie to VDD (see DS11929) |
| 33 | PD0 | digital-IO | GPIO (PD0) | per design |
| 34 | PD1 | digital-IO | GPIO (PD1) | per design |
| 35 | PB13 | digital-IO | GPIO (PB13) | per design |
| 36 | PC6 | digital-IO | GPIO (PC6) | per design |
| 37 | PB14 | digital-IO | GPIO (PB14) | per design |
| 38 | PB15 | digital-IO | GPIO (PB15) | per design |
| 39 | PB6 | digital-IO | GPIO (PB6) | per design |
| 40 | PC13 | digital-IO | GPIO (PC13) | per design (RTC/TAMP capable) |
| 41 | PB12 | digital-IO | GPIO (PB12) | per design |
| 42 | PE4 | digital-IO | GPIO (PE4) | per design |
| 43 | PB1 | digital-IO | GPIO (PB1) | **Version X only**; NC on Version Y (p.7, p.9) |
| 44 | PB0 | digital-IO | GPIO (PB0) | **Version X only**; NC on Version Y (p.7, p.9) |
| 45 | PC5 | digital-IO | GPIO (PC5) | **sensitive GPIO**: add 3.3 pF cap near pin if used (p.13) |
| 46 | PB11 | digital-IO | GPIO (PB11) | **sensitive GPIO**: add 3.3 pF cap near pin if used (p.13) |
| 47 | PB10 | digital-IO | GPIO (PB10) | **sensitive GPIO**: add 3.3 pF cap near pin if used (p.13) |
| 48 | PB2 | digital-IO | GPIO (PB2) | per design |
| 49 | PC4 | digital-IO | GPIO (PC4) | per design |
| 50 | PA8 | digital-IO | GPIO (PA8) | per design |
| 51 | PA9 | digital-IO | GPIO (PA9) | per design |
| 52 | PA7 | digital-IO | GPIO (PA7) | per design |
| 53 | PA6 | digital-IO | GPIO (PA6) | per design |
| 54 | PA5 | digital-IO | GPIO (PA5) | per design |
| 55 | PA4 | digital-IO | GPIO (PA4) | per design |
| 56 | PA3 | digital-IO | GPIO (PA3) | per design |
| 57 | VSS | ground | Ground | GND |
| 58 | ANT_IN | I/O (RF) | Antenna feed | **MUST connect to GND** — internal antenna (p.10) |
| 59 | RF_OUT | I/O (RF) | RF output | **MUST connect to GND** — internal antenna (p.10) |
| 60 | VSS | ground | Ground | GND |
| 61 | PH0 | digital-IO | GPIO (PH0) | per design |
| 62 | PH1 | digital-IO | GPIO (PH1) | per design |
| 63 | PD14 | digital-IO | GPIO (PD14) | per design |
| 64 | PE1 | digital-IO | GPIO (PE1) | per design |
| 65 | PD13 | digital-IO | GPIO (PD13) | per design |
| 66 | PD12 | digital-IO | GPIO (PD12) | per design |
| 67 | PD7 | digital-IO | GPIO (PD7) | per design |
| 68 | PD2 | digital-IO | GPIO (PD2) | per design |
| 69 | PC9 | digital-IO | GPIO (PC9) | per design |
| 70 | PD3 | digital-IO | GPIO (PD3) | per design |
| 71 | PC7 | digital-IO | GPIO (PC7) | per design |
| 72 | PE3 | digital-IO | GPIO (PE3) | per design |
| 73 | PD4 | digital-IO | GPIO (PD4) | per design |
| 74 | PD9 | digital-IO | GPIO (PD9) | per design |
| 75 | PD8 | digital-IO | GPIO (PD8) | per design |
| 76 | PD15 | digital-IO | GPIO (PD15) | per design |
| 77 | PD10 | digital-IO | GPIO (PD10) | per design |
| 78 | PE2 | digital-IO | GPIO (PE2) | per design |
| 79 | PE0 | digital-IO | GPIO (PE0) | per design |
| 80 | PD5 | digital-IO | GPIO (PD5) | per design |
| 81 | PD6 | digital-IO | GPIO (PD6) | per design |
| 82 | PD11 | digital-IO | GPIO (PD11) | per design |
| 83 | PC8 | digital-IO | GPIO (PC8) | per design |
| 84 | VSS | ground | Ground | GND |
| 85 | ANT_NC | I/O (mech) | Soldering-planarity pad | **solder to an unconnected pad** on the board — do NOT connect to a net (p.10) |
| 86 | VSS | ground | Ground | GND |

VSS (ground) pins: **5, 16, 31, 57, 60, 84, 86** (p.7–9). Supply pins: **VREF+ = 4, VDDA = 6, VBAT = 15, VDD = 17, VDDUSB = 32** (p.7–8).

## Power & supply
- **VDD operating range: 1.71 V to 3.6 V** (typ 3.3 V) (Table 3, p.19).
- Operating ambient temperature -40 to 85 °C; storage -40 to 125 °C (Table 3, p.19).
- Power-supply requirements are identical to a regular STM32WB55xx; **filtering caps on the supply pins and all SMPS components are already integrated inside the module** (p.4). Detailed VDD/VDDA/VBAT/VREF+ decoupling and absolute-maximum ratings are in DS11929 (p.4, p.19 — "power consumption is identical to the regular STM32WB55... refer to DS11929").
- **Recommended external cap: an additional 4.7 µF may be needed on the supply to eliminate ripple** (Note, p.4; see AN5165).
- **NRST (pin 10): internal pull-up already present; only an external filter capacitor is required — 100 nF to GND** (p.10, Figure 3).
- SMPS: can be ON or BYPASS; integrated passives run the SMPS at 4 MHz (p.4; see AN5246).
- Absolute-maximum ratings are **not stated in this module datasheet** — refer to DS11929.

## Configuration / strap & mode pins
This module exposes only one hardware boot-mode strap; there is no interface-select, I2C-address, or voltage-select strap on the module itself.

| Config pin | Setting | Result |
|-----------|---------|--------|
| PH3-BOOT0 (13) | Left floating / internal pull-down (default) | Boots from **user flash memory** (p.10) |
| PH3-BOOT0 (13) | Tied high via low-resistance pull-up | Selects alternate boot (system bootloader / SRAM per BOOT0 + option bytes — see RM0434) (p.10) |
| NRST (10) | Low | Device held in reset (internal pull-up holds it high otherwise) (p.10) |

Clock-related fixed settings the firmware must respect (hardware is pre-tuned): LSE must run at medium-high drive (`RCC_BDCR_LSEDRV[1:0] = 10`); HSE is pre-tuned (`RCC_HSECR` must not be changed); `HSEGMC[2:0] = 0b011` (p.5). No external crystal may be added — both crystals are internal (p.5).

## Interface
- The module exposes the STM32WB55 GPIO pads; available peripherals include **2× I2C (SMBus/PMBus), 1× USART, 1× LPUART, two SPI up to 32 Mbit/s, 1× SAI, 1× USB 2.0 FS device (crystal-less), TSC, LCD 8×40, timers, 2× DMA** (p.6).
- **The per-pin ALTERNATE-FUNCTION (peripheral) mapping — which physical pin can be I2C SCL/SDA, SPI SCK/MISO/MOSI/NSS, USART TX/RX, etc., plus AF numbers and electrical I/O specs — is NOT in this module datasheet.** It refers to the underlying STM32WB55VGY device datasheet **DS11929** ("The full pin description is available in DS11929", p.6) and RM0434. Reviewers must map I2C/SPI nets for the BMA400/BMP280/MAX17048/W25Q32/e-paper against DS11929's alternate-function table, not this file.
- I2C 7-bit address / SPI CPOL-CPHA / max-clock selection for the *MCU side* are configured in firmware, not by straps. There is no power-up protocol-detection behavior on this part.

## Application circuit / external components
The module integrates the antenna, both crystals, SMPS passives, IPD/RF matching, and supply filtering, so the required external BOM is minimal (p.3, p.4):

| External part | Value | Where | Source |
|---------------|-------|-------|--------|
| NRST filter capacitor | **100 nF** to GND | pin 10 (NRST) | p.10, Figure 3 |
| Supply ripple cap (optional) | **4.7 µF** | VDD rail | p.4 Note (AN5165) |
| Sensitive-GPIO cap (if PB10/PB11/PC5 used) | **3.3 pF**, 0201 or smaller, close to pin | pins 47/46/45 | p.13 |
| ANT_IN / RF_OUT | hard tie to **GND** (no component) | pins 58/59 | p.10 |
| ANT_NC | solder to an **isolated/unconnected** pad | pin 85 | p.10 |

No external inductor, antenna, balun, crystal, sense resistor, or RF matching network is required or permitted — all internal (p.3). Standard per-rail decoupling for VDD/VDDA/VBAT/VREF+/VDDUSB follows DS11929 (values not enumerated in this module datasheet).

## Key electrical specs
- VDD: 1.71–3.6 V (typ 3.3 V) (p.19).
- Tx output power up to +6 dBm; Rx sensitivity -96 dBm (BLE 1 Mbps), -100 dBm (802.15.4); range up to 75 m (p.0).
- Cortex-M4 up to 64 MHz; 1 MB flash, 256 KB SRAM; 68 GPIOs (p.0).
- Thermal (Table 4, p.20): ΨJT 0.38 °C/W, ϴJA 37.36 °C/W, ϴJB 24.58 °C/W, ϴJC 16.21 °C/W.
- **Logic thresholds (VIL/VIH), I/O current limits, and active/sleep current numbers are NOT in this module datasheet — see DS11929** (p.19: "power consumption is identical to the regular STM32WB55... refer to DS11929"). Do not assume from memory.

## Schematic-review checklist
- **ANT_IN (58) and RF_OUT (59) MUST be tied directly to GND** — internal antenna; a missed GND tie or any attempt to route RF out is wrong (p.10).
- **ANT_NC (85) must go to a dedicated unconnected/isolated pad** (mechanical planarity only) — verify it is NOT joined to GND or any signal net (p.10).
- **NRST (10): confirm a 100 nF cap to GND and NO external pull-up** (pull-up is internal) (p.10).
- **PH3-BOOT0 (13): rely on the internal pull-down for boot-from-flash. If a pull-up/strap or test point is present, confirm it does not unintentionally hold BOOT0 high at reset** (p.10).
- **Verify all VSS pins (5, 16, 31, 57, 60, 84, 86) are tied to GND** (p.7–9).
- **VDD (17) and VBAT (15) on the supply rail (1.71–3.6 V).** If VBAT is on the same rail as VDD, confirm acceptable; do not exceed 3.6 V (p.7, p.19).
- **VDDA (6) and VREF+ (4): confirm they are powered/filtered (not left floating)** even if ADC unused — see DS11929 for the required tie; this module datasheet only lists them as supply pins (p.7).
- **VDDUSB (32): if USB not used, confirm it is tied to a valid supply per DS11929 (commonly VDD), not left floating; if USB used, confirm USB DP/DM on PA12/PA11 (29/30)** (p.7).
- **Consider the 4.7 µF supply ripple cap** to suppress power-supply ripple (p.4).
- **If PB10 (47), PB11 (46) or PC5 (45) are used as signals, add a 3.3 pF cap (0201 or smaller) right at the pin and ground-border the track** — these are RF-sensitive GPIOs (p.13).
- **Version check: PB0 (44) and PB1 (43) only exist on Version X.** If the design nets these pins, confirm Version X parts are sourced; on Version Y they are NC (p.7, p.9).
- **Map every I2C/SPI/UART net against DS11929's alternate-function table** — AF availability per pin is NOT in this module datasheet (p.6).
- **Layout (not strictly schematic): keep a large ground plane to the right of the module, no tracks there, vias ~2 mm spacing; module placement per Figure 4 for antenna performance** (p.11, p.13).
