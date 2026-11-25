#include "control/ChannelActions.h"

#include <Arduino.h>

void ChannelActions::loadFromModes(const ModeRange* modes, ShutterVideoMode svm,
                                   ShutterVideoMode modeStyle, uint16_t startDelayMs,
                                   uint16_t stopDelayMs, OpMode opMode) {
    for (int i = 0; i < FUNC_COUNT; i++)
        modes_[i] = modes[i];
    svm_          = svm;
    modeStyle_    = modeStyle;
    startDelayMs_ = startDelayMs;
    stopDelayMs_  = stopDelayMs;
    opMode_       = opMode;
    pendingRec_   = REC_NONE;  // cancel any queued record command on reconfigure
    primed_       = false;
    armPrimed_    = false;
}

static constexpr uint32_t REC_CONFIRM_MS = 4000;  // camera must report recording by then
static constexpr uint32_t KEEP_RETRY_MS  = 5000;  // re-start interval while armed

void ChannelActions::sendRec(Camera& cam, bool start) {
    if (start) {
        cam.startRecord();
        startSentMs_ = millis();
        if (!startSentMs_)
            startSentMs_ = 1;
    } else {
        cam.stopRecord();
        startSentMs_ = 0;
        rejected_    = false;
    }
}

void ChannelActions::trackRejected(const Camera& cam, uint32_t now) {
    if (cam.status().isRecording()) {
        startSentMs_ = 0;
        rejected_    = false;
    } else if (startSentMs_ && now - startSentMs_ >= REC_CONFIRM_MS) {
        startSentMs_ = 0;
        rejected_    = cam.isConnected();  // a dropped link is reported as such, not as this
    }
}

// Record-on-arm: while armed, recording should be on. Covers a camera that connects or
// reconnects mid-flight and a bridge that rebooted while armed. A clip started by hand
// on the camera is simply left running until disarm
void ChannelActions::keepRecording(Camera& cam, uint32_t now) {
    const CameraStatus& st = cam.status();
    if (!cam.isConnected() || !st.ready || st.mode != CamMode::Video || st.isRecording() ||
        pendingRec_ != REC_NONE || startSentMs_)
        return;
    // After a rejection back off further, so a camera that keeps refusing is not hammered
    if (now - lastKeepMs_ < (rejected_ ? 3 * KEEP_RETRY_MS : KEEP_RETRY_MS))
        return;
    lastKeepMs_ = now;
    Serial.println("[rec] armed but not recording - starting");
    sendRec(cam, true);
}

void ChannelActions::scheduleRec(Camera& cam, bool start) {
    const uint16_t delay = start ? startDelayMs_ : stopDelayMs_;
    if (delay == 0) {
        sendRec(cam, start);
        return;
    }
    pendingRec_  = start ? REC_START : REC_STOP;
    pendingAtMs_ = millis() + delay;
}

void ChannelActions::update(const RcState& rc, Camera& cam, bool armed) {
    const uint32_t now = millis();

    if (pendingRec_ != REC_NONE && (int32_t)(now - pendingAtMs_) >= 0) {
        sendRec(cam, pendingRec_ == REC_START);
        pendingRec_ = REC_NONE;
    }
    trackRejected(cam, now);

    // Record-on-arm
    if (opMode_ == OpMode::RecordOnArm) {
        if (!armPrimed_) {
            prevArmed_ = armed;
            armPrimed_ = true;
        } else if (armed != prevArmed_) {
            prevArmed_ = armed;
            if (armed) {
                rejected_   = false;  // fresh attempt each flight
                lastKeepMs_ = now;    // the edge itself starts it; retry only after that
            }
            scheduleRec(cam, armed);
        }
        if (armed)
            keepRecording(cam, now);
        return;
    }

    if (!rc.valid)
        return;

    bool active[FUNC_COUNT];
    for (int i = 0; i < FUNC_COUNT; i++) {
        const ModeRange& m = modes_[i];
        if (m.aux == 0) {
            active[i] = false;
        } else {
            const uint16_t v = rc.aux(m.aux);
            active[i]        = (v >= m.rangeMin && v <= m.rangeMax);
        }
    }

    if (!primed_) {
        for (int i = 0; i < FUNC_COUNT; i++)
            prevActive_[i] = active[i];
        primed_ = true;
        return;
    }

    for (int i = 0; i < FUNC_COUNT; i++) {
        if (active[i] == prevActive_[i])
            continue;
        const Func f      = (Func)i;
        const bool rising = active[i];
        prevActive_[i]    = active[i];

        if (f == Func::Shutter) {
            const bool photo = cam.status().mode == CamMode::Photo;
            if (photo) {
                if (rising)
                    cam.takePhoto();
            } else if (svm_ == ShutterVideoMode::TwoPos) {
                scheduleRec(cam, rising);
            } else if (rising) {
                scheduleRec(cam, !cam.status().isRecording());
            }
            continue;
        }

        if (f == Func::CameraMode) {
            if (modeStyle_ == ShutterVideoMode::TwoPos) {
                cam.setMode(rising ? CamMode::Photo : CamMode::Video);
            } else if (rising) {
                cam.setMode(cam.status().mode == CamMode::Photo ? CamMode::Video : CamMode::Photo);
            }
            continue;
        }

        if (rising) {
            switch (f) {
                case Func::PresetVideo:
                    cam.loadPresetGroup(PresetGroup::Video);
                    break;
                case Func::PresetPhoto:
                    cam.loadPresetGroup(PresetGroup::Photo);
                    break;
                case Func::PresetTimelapse:
                    cam.loadPresetGroup(PresetGroup::Timelapse);
                    break;
                default:
                    break;
            }
        }
    }
}
