#include "osd/OsdRenderer.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

void OsdRenderer::formatField(OsdField f, const CameraStatus& st, bool online, char* out,
                              size_t n, bool& blink) {
    out[0] = '\0';
    blink  = false;
    if (!online) {
        // Never connected this flight: stay out of the way. Lost mid-flight: say so
        if (f == OsdField::RecStatus && !(armed_ && !seenInArm_)) {
            snprintf(out, n, "%s", armed_ ? "DISCONNECTED" : "CAM OFF");
            blink = armed_;
        }
        return;
    }

    const bool noSd       = st.sdFreeMB == 0 && st.recLeftS == 0;
    const bool lowBattery = s_.camBattWarnPct && st.batteryPct &&
                            st.batteryPct <= s_.camBattWarnPct;
    const bool lowStorage =
        s_.recLeftWarnMin && st.recLeftS && st.recLeftS < (uint32_t)s_.recLeftWarnMin * 60;

    switch (f) {
        case OsdField::RecStatus:
            if (!st.ready) {
                snprintf(out, n, "CAM NOT READY");  // not in a photo/video shooting mode
            } else if (s_.opMode == (uint8_t)OpMode::RecordOnArm && st.mode == CamMode::Photo) {
                snprintf(out, n, "PHOTO NO-REC!");  // record-on-arm can't record in photo mode
            } else if (st.isRecording()) {
                snprintf(out, n, "REC %u:%02u", (unsigned)(st.recElapsedS / 60),
                         (unsigned)(st.recElapsedS % 60));
            } else if (rejected_) {
                snprintf(out, n, "REC REJECTED");
                blink = true;
            } else if (st.mode == CamMode::Video && noSd) {
                snprintf(out, n, "NO SD!");
                blink = true;
            } else if (armed_) {
                snprintf(out, n, "NOT REC");  // flying without a clip
                blink = true;
            } else {
                snprintf(out, n, "CAM READY");
            }
            break;
        case OsdField::Battery:
            snprintf(out, n, lowBattery ? "CAM %u%%!" : "CAM %u%%", (unsigned)st.batteryPct);
            blink = lowBattery;
            break;
        case OsdField::Mode:
            snprintf(out, n, "%s", st.mode == CamMode::Photo ? "PHOTO" : "VIDEO");
            break;
        case OsdField::SdFree:
            if (noSd)
                snprintf(out, n, "NO SD!");
            else
                snprintf(out, n, lowStorage ? "%uG!" : "%uG",
                         (unsigned)((st.sdFreeMB + 512) / 1024));
            blink = noSd || lowStorage;
            break;
        case OsdField::TimeLeft:
            if (st.recLeftS < 3600)
                snprintf(out, n, lowStorage ? "%u:%02u!" : "%u:%02u",
                         (unsigned)(st.recLeftS / 60), (unsigned)(st.recLeftS % 60));
            else
                snprintf(out, n, "%uH%02u", (unsigned)(st.recLeftS / 3600),
                         (unsigned)(st.recLeftS / 60 % 60));
            blink = lowStorage;
            break;
        case OsdField::Resolution:
            // Blank (cleared slot) when unreported
            snprintf(out, n, "%s", st.resolution);
            break;
        case OsdField::Fps:
            if (st.fps)
                snprintf(out, n, "%uFPS", (unsigned)st.fps);
            break;
        case OsdField::Format:
            // "4K60": either half is left out when the camera does not report it
            if (st.fps)
                snprintf(out, n, "%s%u", st.resolution, (unsigned)st.fps);
            else
                snprintf(out, n, "%s", st.resolution);
            break;
        case OsdField::Off:
        default:
            break;
    }
}

void OsdRenderer::update(const CameraStatus& st, bool online, bool armed, bool rejected) {
    const uint32_t now = millis();
    if (now - lastMs_ < updateMs_)
        return;
    lastMs_ = now;

    if (armed && !armed_)
        seenInArm_ = online;  // new flight
    else if (armed && online)
        seenInArm_ = true;
    armed_                  = armed;
    rejected_               = rejected;
    const bool blinkVisible = (now / 500) % 2 == 0;  // warnings flash at 1 Hz

    // The FC/goggle OSD retains the last strings we drew, and we normally only re-send a slot
    // when its text changes. If an early write is lost (e.g. FC still booting), a stale frame
    // would linger forever. Periodically re-send every slot so the display always converges.
    bool forceAll = false;
    if (now - lastRefreshMs_ >= refreshMs_) {
        lastRefreshMs_ = now;
        forceAll       = true;
    }

    for (uint8_t slot = 0; slot < 4; slot++) {
        bool blink = false;
        formatField(s_.osdSlot[slot], st, online, preview_[slot], sizeof(preview_[slot]), blink);
        blinks_[slot] = blink;

        char buf[32];
        if (blink && !blinkVisible)
            buf[0] = '\0';
        else
            strncpy(buf, preview_[slot], sizeof(buf));
        if (buf[0] == '\0') {
            buf[0] = ' ';
            buf[1] = '\0';
        }
        if (forceAll || strncmp(buf, last_[slot], sizeof(buf)) != 0) {
            strncpy(last_[slot], buf, sizeof(last_[slot]));
            fc_.setOsdMessage(slot, buf);  // empty string clears the element
        }
    }
}
