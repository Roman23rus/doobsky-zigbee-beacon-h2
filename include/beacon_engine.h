#pragma once

#include <Arduino.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

class BeaconEngine {
public:
  BeaconEngine() = default;
  BeaconEngine(const BeaconEngine &) = delete;
  BeaconEngine &operator=(const BeaconEngine &) = delete;

  bool begin();
  void request(bool on, uint8_t level);
  bool isDarkWindowSafe() const;
  void forceOff();

private:
  static void taskEntry(void *arg);
  static void timerCallback(void *arg);

  void taskLoop();
  void applyRequestedState();
  int64_t serviceAt(int64_t nowUs);
  void armDeadline(int64_t deadlineUs);
  void stopDeadline();
  void writePwm(uint16_t duty, int64_t nowUs);
  void writeRgb(uint8_t level, int64_t nowUs);
  uint16_t peakDutyForLevel(uint8_t level) const;

  mutable portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
  TaskHandle_t taskHandle_ = nullptr;
  esp_timer_handle_t timerHandle_ = nullptr;

  bool requestedOn_ = false;
  uint8_t requestedLevel_ = 0;
  bool requestDirty_ = false;
  bool restartCycleRequested_ = false;

  bool activeOn_ = false;
  uint8_t activeLevel_ = 0;
  uint16_t activePeakDuty_ = 0;

  int64_t cycleEpochUs_ = 0;
  int64_t nextDeadlineUs_ = 0;
  uint16_t lastPwmDuty_ = 0;
  uint8_t lastRgbLevel_ = 0xFF;
  bool lastRgbEnabled_ = false;
  int64_t lastRgbUpdateUs_ = 0;
};
