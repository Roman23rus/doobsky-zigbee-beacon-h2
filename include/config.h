#pragma once

#include <Arduino.h>

namespace BeaconConfig {

// -----------------------------------------------------------------------------
// Firmware identity
// -----------------------------------------------------------------------------
inline constexpr char FW_VERSION[] = "0.8.0-alpha.1";
inline constexpr char MANUFACTURER[] = "Doobsky";
inline constexpr char MODEL[] = "DBL-01";
inline constexpr uint8_t BATTERY_SENSOR_ENDPOINT = 1;
inline constexpr uint8_t LIGHT_ENDPOINT = 2;
inline constexpr uint8_t ZIGBEE_APP_VERSION = 1;
inline constexpr uint8_t ZIGBEE_HW_VERSION = 1;
inline constexpr uint8_t ZIGBEE_MAX_LEVEL = 254;

// -----------------------------------------------------------------------------
// ESP32-H2 SuperMini pins
// -----------------------------------------------------------------------------
// SuperMini: BOOT is GPIO9, user-controllable blue LED is GPIO13.
// GPIO10 is a safe free pin and drives the external AO3400A gate via 100 ohm.
inline constexpr uint8_t MOSFET_PWM_PIN = 10;
inline constexpr uint8_t STATUS_LED_PIN = 13;
inline constexpr uint8_t RGB_LED_PIN = 8;
inline constexpr uint8_t BOOT_BUTTON_PIN = 9;
inline constexpr uint8_t BATTERY_ADC_PIN = 1;

// Change to false only if your particular SuperMini clone has inverted blue LED.
inline constexpr bool STATUS_LED_ACTIVE_HIGH = true;
inline constexpr uint32_t STATUS_LED_PWM_FREQUENCY_HZ = 5000;
inline constexpr uint8_t STATUS_LED_PWM_RESOLUTION_BITS = 8;
inline constexpr uint32_t ZIGBEE_BREATHE_PERIOD_MS = 1600;
inline constexpr uint32_t RGB_UPDATE_US = 10'000;  // 100 Hz is visually smooth and halves WS2812 writes
inline constexpr uint8_t RGB_STATUS_RED_LEVEL = 77;  // 30% steady red between beacon flashes
inline constexpr uint32_t STATUS_UPDATE_INTERVAL_MS = 20;
inline constexpr uint32_t BUTTON_POLL_INTERVAL_MS = 20;
inline constexpr uint32_t HOUSEKEEPING_INTERVAL_MS = 20;

// Keep immediate Zigbee command response while reducing CPU dynamic power.
// ESP32-H2 officially supports 64 MHz; 802.15.4/APB peripherals remain clocked independently.
inline constexpr uint32_t CPU_FREQUENCY_MHZ = 64;
inline constexpr bool ZIGBEE_RX_ON_WHEN_IDLE = true;

// 1S Li-ion battery monitor: BAT+ -> 100k -> GPIO1 -> 100k -> GND.
// Add 100 nF from GPIO1 to GND; use 1% resistors for good accuracy.
inline constexpr float BATTERY_DIVIDER_RATIO = 2.0f;
inline constexpr float BATTERY_CALIBRATION = 1.0f;
inline constexpr uint8_t BATTERY_ADC_SAMPLES = 32;
inline constexpr uint32_t BATTERY_SAMPLE_INTERVAL_MS = 60000;
inline constexpr uint32_t BATTERY_REPORT_INTERVAL_MS = 300000;
inline constexpr uint32_t BATTERY_REPORT_RETRY_MS = 1000;
inline constexpr uint8_t BATTERY_REPORT_DELTA_PERCENT = 2;
inline constexpr uint16_t BATTERY_REPORT_DELTA_MV = 20;
inline constexpr uint16_t BATTERY_VALID_MIN_MV = 2500;
inline constexpr uint16_t BATTERY_VALID_MAX_MV = 4400;

// -----------------------------------------------------------------------------
// PWM
// -----------------------------------------------------------------------------
inline constexpr uint32_t PWM_FREQUENCY_HZ = 1000;  // validated with the 12 V LED module
inline constexpr uint8_t PWM_RESOLUTION_BITS = 10;
inline constexpr uint16_t PWM_MAX = (1U << PWM_RESOLUTION_BITS) - 1U;
inline constexpr uint32_t ENVELOPE_UPDATE_US = 1000; // 1 ms envelope resolution
inline constexpr int64_t BACKGROUND_WORK_GUARD_US = 50'000;
inline constexpr uint8_t BEACON_TASK_PRIORITY = 4;
inline constexpr uint32_t BEACON_TASK_STACK_SIZE = 4096;

// -----------------------------------------------------------------------------
// Doobsky lighthouse light characteristic
// -----------------------------------------------------------------------------
// Confirmed characteristic: Fl(3).W.16.5s (three white flashes / 16.5 s cycle).
//
// IMPORTANT:
// The individual flash/dark durations below are the current engineering
// reconstruction based on scaling an EMV-3 0.3/2.6/0.3/2.6/0.3/7.9 s cycle
// from 14.0 s to 16.5 s. The *cycle period* is exact; these individual values
// must be replaced if/when a primary Doobsky light-list entry gives the exact
// element durations.
inline constexpr uint32_t CYCLE_US       = 16'500'000;
inline constexpr uint32_t FLASH_US       =    353'571;
inline constexpr uint32_t SHORT_DARK_US  =  3'064'286;
inline constexpr uint32_t LONG_DARK_US   =  9'310'715;

static_assert(3ULL * FLASH_US + 2ULL * SHORT_DARK_US + LONG_DARK_US == CYCLE_US,
              "Beacon timing must sum to exactly 16.5 seconds");

inline constexpr uint32_t FLASH2_START_US = FLASH_US + SHORT_DARK_US;
inline constexpr uint32_t FLASH3_START_US = 2U * (FLASH_US + SHORT_DARK_US);

// -----------------------------------------------------------------------------
// Local UI / persistence
// -----------------------------------------------------------------------------
inline constexpr uint32_t BUTTON_DEBOUNCE_MS = 35;
inline constexpr uint32_t FACTORY_RESET_HOLD_MS = 5000;
inline constexpr uint32_t PREFS_WRITE_DELAY_MS = 1500;
inline constexpr bool RESTORE_OUTPUT_AFTER_REBOOT = true;
inline constexpr uint8_t DEFAULT_LEVEL = 254;

} // namespace BeaconConfig
