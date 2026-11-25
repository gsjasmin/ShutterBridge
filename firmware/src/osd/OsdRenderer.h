// Renders CameraStatus into the FC's 4 configurable "Custom Msg" OSD slots.
#pragma once

#include <cstddef>
#include <cstdint>

#include "camera/Camera.h"
#include "config/Settings.h"
#include "fc/FlightController.h"

class OsdRenderer {
   public:
    OsdRenderer(FlightController& fc, Settings& s, uint32_t updateMs = 250,
                uint32_t refreshMs = 1000)
        : fc_(fc), s_(s), updateMs_(updateMs), refreshMs_(refreshMs) {
    }

    // `online` = camera connected AND telemetry is fresh. `rejected` = the camera did not
    // start recording after a record command (ChannelActions::recRejected)
    void update(const CameraStatus& st, bool online, bool armed, bool rejected);

    // For the Web UI preview: a slot's text without the blink applied, and whether the
    // goggles see it flashing (a warning). "" = the slot is blank
    const char* previewText(uint8_t slot) const {
        return slot < 4 ? preview_[slot] : "";
    }
    bool previewBlinks(uint8_t slot) const {
        return slot < 4 && blinks_[slot];
    }

   private:
    // Writes the slot text; sets `blink` when it is a warning that should flash
    void formatField(OsdField f, const CameraStatus& st, bool online, char* out, size_t n,
                     bool& blink);

    bool armed_     = false;
    bool rejected_  = false;
    bool seenInArm_ = false;  // camera was online at some point during this flight

    FlightController& fc_;
    Settings&         s_;
    uint32_t          updateMs_;
    uint32_t          refreshMs_;
    uint32_t          lastMs_        = 0;
    uint32_t          lastRefreshMs_ = 0;
    char              last_[4][32]   = {{0}};  // what the FC was last sent
    char              preview_[4][32] = {{0}};
    bool              blinks_[4]      = {false};
};
