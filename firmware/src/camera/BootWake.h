#pragma once

#include <Arduino.h>

// False after a software reset (camera switch, settings save, reboot button): only a real
// power-up wakes the camera, so switching to a camera never turns on one the user left off.
// Set by main before the camera task starts
extern bool g_bootWakeAllowed;

class BootWake {
   public:
    void start() {
        startedMs_ = millis();
        active_    = true;
    }
    void finish() {
        active_ = false;
    }
    bool active() const {
        return active_;
    }
    uint32_t ageMs() const {
        return millis() - startedMs_;
    }
    bool expired() const {
        return active_ && ageMs() > kTimeoutMs;
    }

    static constexpr uint32_t kTimeoutMs = 30000;

   private:
    bool     active_    = false;
    uint32_t startedMs_ = 0;
};
