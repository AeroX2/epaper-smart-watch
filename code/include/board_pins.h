#pragma once

#include <Arduino.h>

namespace BoardPins {

// Shared I2C1 bus.
constexpr uint32_t I2C_SDA = PB9;
constexpr uint32_t I2C_SCL = PB8;

// Sensor and fuel-gauge interrupt inputs.
constexpr uint32_t ACCEL_INT1 = PC0;
constexpr uint32_t ACCEL_INT2 = PC1;
constexpr uint32_t BATTERY_ALERT = PB3;

// Active-low user buttons.
constexpr uint32_t BUTTON_1 = PC10;
constexpr uint32_t BUTTON_2 = PC2;
constexpr uint32_t BUTTON_3 = PC12;
constexpr uint32_t BUTTON_4 = PB4;

// nPM1100 control pins. Keep ISET low until USB current has been negotiated.
constexpr uint32_t CHARGER_ISET = PD0;
constexpr uint32_t CHARGER_MODE = PA9;
constexpr uint32_t SHIP_HOLD = PC6;

// Low-side actuator FET gates.
constexpr uint32_t VIBRATION = PA15;
constexpr uint32_t BUZZER = PE0;

// ET011TJ1 four-wire SPI interface. The display SDA line is write-only here.
constexpr uint32_t DISPLAY_SCK = PA5;
constexpr uint32_t DISPLAY_MOSI = PA7;
// The STM32 Arduino SPI core requires a valid MISO pin even for transmit-only
// buses. PB4 is physically button 4, not connected to the display data line,
// and is used only to let the core initialize SPI1.
constexpr uint32_t DISPLAY_DUMMY_MISO = PB4;
constexpr uint32_t DISPLAY_CS = PA4;
constexpr uint32_t DISPLAY_DC = PA8;
constexpr uint32_t DISPLAY_RESET = PA6;
constexpr uint32_t DISPLAY_BUSY = PA2;
constexpr uint32_t DISPLAY_LIGHT = PC3;

}  // namespace BoardPins
