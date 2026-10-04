#include "beacon_engine.h"

#include "config.h"
#include "flash_envelope_lut.h"
#include "esp32-hal-rgb-led.h"

using namespace BeaconConfig;

bool BeaconEngine::begin() {
  rgbLedWrite(RGB_LED_PIN, 0, 0, 0);
  lastRgbLevel_ = 0;
  lastRgbEnabled_ = false;
  lastRgbUpdateUs_ = esp_timer_get_time();

  if (!ledcAttach(MOSFET_PWM_PIN, PWM_FREQUENCY_HZ, PWM_RESOLUTION_BITS)) {
    return false;
  }
  ledcWrite(MOSFET_PWM_PIN, 0);
  lastPwmDuty_ = 0;

  esp_timer_create_args_t timerArgs{};
  timerArgs.callback = &BeaconEngine::timerCallback;
  timerArgs.arg = this;
  timerArgs.dispatch_method = ESP_TIMER_TASK;
  timerArgs.name = "beacon_wake";
  if (esp_timer_create(&timerArgs, &timerHandle_) != ESP_OK) {
    return false;
  }

  if (xTaskCreate(&BeaconEngine::taskEntry,
                  "BeaconEngine",
                  BEACON_TASK_STACK_SIZE,
                  this,
                  BEACON_TASK_PRIORITY,
                  &taskHandle_) != pdPASS) {
    esp_timer_delete(timerHandle_);
    timerHandle_ = nullptr;
    return false;
  }
  return true;
}

void BeaconEngine::request(bool on, uint8_t level) {
  TaskHandle_t task = nullptr;
  portENTER_CRITICAL(&mux_);
  const bool turningOn = on && !requestedOn_;
  requestedOn_ = on;
  requestedLevel_ = level;
  requestDirty_ = true;
  if (turningOn) {
    restartCycleRequested_ = true;
  }
  task = taskHandle_;
  portEXIT_CRITICAL(&mux_);

  if (task != nullptr) {
    xTaskNotifyGive(task);
  }
}

bool BeaconEngine::isDarkWindowSafe() const {
  bool on;
  uint16_t duty;
  int64_t deadline;
  portENTER_CRITICAL(&mux_);
  on = activeOn_;
  duty = lastPwmDuty_;
  deadline = nextDeadlineUs_;
  portEXIT_CRITICAL(&mux_);

  if (duty != 0) return false;
  if (!on || deadline == 0) return true;
  return (deadline - esp_timer_get_time()) > BACKGROUND_WORK_GUARD_US;
}

void BeaconEngine::forceOff() {
  uint8_t level;
  portENTER_CRITICAL(&mux_);
  level = requestedLevel_;
  portEXIT_CRITICAL(&mux_);
  request(false, level);
}

void BeaconEngine::taskEntry(void *arg) {
  static_cast<BeaconEngine *>(arg)->taskLoop();
}

void BeaconEngine::timerCallback(void *arg) {
  auto *self = static_cast<BeaconEngine *>(arg);
  if (self != nullptr && self->taskHandle_ != nullptr) {
    xTaskNotifyGive(self->taskHandle_);
  }
}

void BeaconEngine::taskLoop() {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    stopDeadline();

    const int64_t nowUs = esp_timer_get_time();
    applyRequestedState(nowUs);
    const int64_t next = serviceAt(nowUs);

    portENTER_CRITICAL(&mux_);
    nextDeadlineUs_ = next;
    portEXIT_CRITICAL(&mux_);

    if (next > 0) {
      armDeadline(next);
    }
  }
}

void BeaconEngine::applyRequestedState(int64_t nowUs) {
  bool dirty;
  bool newOn;
  bool restartCycle;
  uint8_t newLevel;

  portENTER_CRITICAL(&mux_);
  dirty = requestDirty_;
  newOn = requestedOn_;
  newLevel = requestedLevel_;
  restartCycle = restartCycleRequested_;
  requestDirty_ = false;
  restartCycleRequested_ = false;
  portEXIT_CRITICAL(&mux_);

  if (!dirty) return;

  const uint16_t newPeakDuty = peakDutyForLevel(newLevel);
  const bool forceDark = !newOn || newPeakDuty == 0;

  activePeakDuty_ = newPeakDuty;
  if (forceDark) {
    cycleEpochUs_ = 0;
  } else if (restartCycle || cycleEpochUs_ == 0) {
    cycleEpochUs_ = nowUs;
  }

  portENTER_CRITICAL(&mux_);
  activeOn_ = newOn;
  if (forceDark) {
    nextDeadlineUs_ = 0;
  }
  portEXIT_CRITICAL(&mux_);

  if (forceDark) {
    writePwm(0, nowUs);
  }
}

int64_t BeaconEngine::serviceAt(int64_t nowUs) {
  if (!activeOn_ || activePeakDuty_ == 0) {
    writePwm(0, nowUs);
    return 0;
  }

  if (cycleEpochUs_ == 0) {
    cycleEpochUs_ = nowUs;
  }

  int64_t elapsedUs = nowUs - cycleEpochUs_;
  if (elapsedUs >= CYCLE_US) {
    const int64_t cycles = elapsedUs / CYCLE_US;
    cycleEpochUs_ += cycles * CYCLE_US;
    elapsedUs -= cycles * CYCLE_US;
  }

  const uint32_t phaseUs = static_cast<uint32_t>(elapsedUs);
  uint32_t localUs = 0;
  int64_t flashStartUs = 0;
  int64_t flashEndUs = 0;

  if (phaseUs < FLASH_US) {
    localUs = phaseUs;
    flashStartUs = cycleEpochUs_;
    flashEndUs = cycleEpochUs_ + FLASH_US;
  } else if (phaseUs < FLASH2_START_US) {
    writePwm(0, nowUs);
    return cycleEpochUs_ + FLASH2_START_US;
  } else if (phaseUs < FLASH2_START_US + FLASH_US) {
    localUs = phaseUs - FLASH2_START_US;
    flashStartUs = cycleEpochUs_ + FLASH2_START_US;
    flashEndUs = flashStartUs + FLASH_US;
  } else if (phaseUs < FLASH3_START_US) {
    writePwm(0, nowUs);
    return cycleEpochUs_ + FLASH3_START_US;
  } else if (phaseUs < FLASH3_START_US + FLASH_US) {
    localUs = phaseUs - FLASH3_START_US;
    flashStartUs = cycleEpochUs_ + FLASH3_START_US;
    flashEndUs = flashStartUs + FLASH_US;
  } else {
    writePwm(0, nowUs);
    return cycleEpochUs_ + CYCLE_US;
  }

  constexpr uint32_t requiredSamples =
      (FLASH_US + ENVELOPE_UPDATE_US - 1U) / ENVELOPE_UPDATE_US;
  static_assert(FLASH_ENVELOPE_LUT_SIZE >= requiredSamples,
                "Flash envelope LUT is too short");

  const uint32_t sampleIndex = localUs / ENVELOPE_UPDATE_US;
  const uint16_t fullDuty = FLASH_ENVELOPE_LUT[sampleIndex];
  const uint16_t duty = activePeakDuty_ == PWM_MAX
      ? fullDuty
      : static_cast<uint16_t>(
            (static_cast<uint32_t>(fullDuty) * activePeakDuty_ + PWM_MAX / 2U) /
            PWM_MAX);
  writePwm(duty, nowUs);

  int64_t next =
      flashStartUs + static_cast<int64_t>(sampleIndex + 1U) * ENVELOPE_UPDATE_US;
  if (next > flashEndUs) next = flashEndUs;
  return next;
}

void BeaconEngine::armDeadline(int64_t deadlineUs) {
  if (timerHandle_ == nullptr) return;

  const int64_t nowUs = esp_timer_get_time();
  const int64_t delayUs = deadlineUs - nowUs;
  if (delayUs <= 0) {
    if (taskHandle_ != nullptr) xTaskNotifyGive(taskHandle_);
    return;
  }

  if (esp_timer_start_once(timerHandle_, static_cast<uint64_t>(delayUs)) != ESP_OK) {
    writePwm(0, nowUs);
    ESP.restart();
  }
}

void BeaconEngine::stopDeadline() {
  if (timerHandle_ != nullptr) {
    esp_timer_stop(timerHandle_);
  }
}

void BeaconEngine::writePwm(uint16_t duty, int64_t nowUs) {
  const bool dutyChanged = duty != lastPwmDuty_;
  if (dutyChanged) {
    ledcWrite(MOSFET_PWM_PIN, duty);
    portENTER_CRITICAL(&mux_);
    lastPwmDuty_ = duty;
    portEXIT_CRITICAL(&mux_);
  }

  const uint8_t rgb = static_cast<uint8_t>(
      (static_cast<uint32_t>(duty) * 255U + PWM_MAX / 2U) / PWM_MAX);

  // If PWM, RGB target and enabled state are unchanged, there is no work left.
  // A throttled RGB update leaves lastRgbLevel_ behind the target, so it will
  // still be retried on a later envelope tick.
  if (!dutyChanged && rgb == lastRgbLevel_ &&
      activeOn_ == lastRgbEnabled_) {
    return;
  }

  writeRgb(rgb, nowUs);
}

void BeaconEngine::writeRgb(uint8_t level, int64_t nowUs) {
  const bool enabled = activeOn_;

  if (level == lastRgbLevel_ && enabled == lastRgbEnabled_) return;

  if (!enabled) {
    rgbLedWrite(RGB_LED_PIN, 0, 0, 0);
    lastRgbLevel_ = level;
    lastRgbEnabled_ = false;
    lastRgbUpdateUs_ = nowUs;
    return;
  }

  if (level != 0 && lastRgbEnabled_ && lastRgbUpdateUs_ != 0 &&
      (nowUs - lastRgbUpdateUs_) < RGB_UPDATE_US) {
    return;
  }

  // When the beacon is enabled, keep red at 30% between flashes and
  // smoothly blend toward white using the same envelope as the beacon.
  const uint8_t red =
      level > RGB_STATUS_RED_LEVEL ? level : RGB_STATUS_RED_LEVEL;
  rgbLedWrite(RGB_LED_PIN, red, level, level);
  lastRgbLevel_ = level;
  lastRgbEnabled_ = true;
  lastRgbUpdateUs_ = nowUs;
}

uint16_t BeaconEngine::peakDutyForLevel(uint8_t level) const {
  if (level == 0 || level > ZIGBEE_MAX_LEVEL) return 0;
  return static_cast<uint16_t>(
      (static_cast<uint32_t>(level) * PWM_MAX + ZIGBEE_MAX_LEVEL / 2U) /
      ZIGBEE_MAX_LEVEL);
}
