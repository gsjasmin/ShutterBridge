#include "fc/BetaflightMsp.h"

#include <stdio.h>
#include <string.h>

BetaflightMsp::BetaflightMsp(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin,
                             uint32_t rcPollMs)
    : serial_(serial), baud_(baud), rxPin_(rxPin), txPin_(txPin), rcPollMs_(rcPollMs) {
}

void BetaflightMsp::setPins(int rxPin, int txPin) {
    rxPin_ = rxPin;
    txPin_ = txPin;
}

void BetaflightMsp::begin() {
    serial_.begin(baud_, SERIAL_8N1, rxPin_, txPin_);
    Serial.printf("[fc] UART rx=%d tx=%d\n", rxPin_, txPin_);
}

void BetaflightMsp::poll() {
    const uint32_t now = millis();

    // Drain RX and parse any complete reply frames
    while (serial_.available()) {
        const int c = serial_.read();
        if (c < 0)
            break;
        const uint8_t b = (uint8_t)c;
        parser_.feed(&b, 1, [this](const msp::Message& m) { onMessage(m); });
    }

    sendQueuedText(now);

    if (!identSent_ && replies_[R_RC] > 0) {
        identSent_ = true;
        sendIdent();
    }

    // Periodically ask for RC channels
    if (now - lastReqMs_ >= rcPollMs_) {
        lastReqMs_ = now;
        requestRc();
    }

    // Poll the RTC quickly until we have a time, then only occasionally
    const uint32_t rtcInterval = time_.valid ? 30000 : 3000;
    if (now - lastRtcReqMs_ >= rtcInterval) {
        lastRtcReqMs_ = now;
        requestRtc();
    }

    // ARM state: locate ARM's flag bit once (MSP_BOXIDS), then poll MSP_STATUS
    if (armBit_ < 0 && now - lastBoxIdsMs_ >= 2000) {
        lastBoxIdsMs_ = now;
        // FCs that answer MSP but not MSP_BOXIDS (KISS Ultra): ARM is box 0, i.e. flag bit 0
        // as on Betaflight. Only fall back once the FC is clearly talking MSP
        if (++boxIdsTries_ > 3 && replies_[R_RC] > 0) {
            armBit_        = 0;
            armBitGuessed_ = true;
            Serial.println("[fc] no MSP_BOXIDS reply - assuming ARM is flag bit 0");
        } else {
            requestBoxIds();
        }
    }
    if (armBit_ >= 0 && now - lastStatusMs_ >= 200) {
        lastStatusMs_ = now;
        requestStatus();
    }
}

void BetaflightMsp::requestRc() {
    auto f = msp::encodeV1(msp::MSP_RC);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::requestRtc() {
    auto f = msp::encodeV1(msp::MSP_RTC);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::requestStatus() {
    auto f = msp::encodeV1(msp::MSP_STATUS);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::requestBoxIds() {
    auto f = msp::encodeV1(msp::MSP_BOXIDS);
    serial_.write(f.data(), f.size());
}

void BetaflightMsp::sendIdent() {
    // API_VERSION, FC_VARIANT, FC_VERSION, BOARD_INFO, NAME - logged by logOther()
    static const uint8_t cmds[] = {1, 2, 3, 4, 10};
    for (uint8_t c : cmds) {
        auto f = msp::encodeV1(c);
        serial_.write(f.data(), f.size());
    }
}

void BetaflightMsp::logOther(const msp::Message& m) {
    if (otherLogged_ >= 16)
        return;
    otherLogged_++;
    Serial.print("[fc] reply ");
    logFrame(m);
}

void BetaflightMsp::logFrame(const msp::Message& m) {
    char hex[3 * 16 + 1] = {0}, asc[17] = {0};
    const size_t n = m.len < 16 ? m.len : 16;
    for (size_t i = 0; i < n; i++) {
        snprintf(&hex[3 * i], 4, "%02x ", m.payload[i]);
        asc[i] = (m.payload[i] >= 32 && m.payload[i] < 127) ? (char)m.payload[i] : '.';
    }
    Serial.printf("%s cmd=%u (0x%04x)%s len=%u: %s|%s|\n", m.v2 ? "v2" : "v1", m.cmd, m.cmd,
                  m.error ? " ERR" : "", (unsigned)m.len, hex, asc);
}

void BetaflightMsp::onMessage(const msp::Message& m) {
    // MSP v2 tunnelled in a v1 frame (cmd 255): flags, cmd u16, size u16, payload, crc8
    if (!m.v2 && m.cmd == 255 && m.len >= 6) {
        const uint16_t cmd = (uint16_t)m.payload[1] | ((uint16_t)m.payload[2] << 8);
        const uint16_t sz  = (uint16_t)m.payload[3] | ((uint16_t)m.payload[4] << 8);
        if ((size_t)sz + 6 <= m.len) {
            msp::Message in{true, m.error, m.request, cmd, m.payload + 5, sz};
            onMessage(in);
            return;
        }
    }
    if (m.request) {
        // The FC is polling us (KISS talks to CamCandy as MSP master). Log what it asks for
        if (fcRequests_ < 0xFFFF)
            fcRequests_++;
        if (reqLogged_ < 24) {
            reqLogged_++;
            Serial.print("[fc] FC request: ");
            logFrame(m);
        }
        return;
    }
    int r = -1;
    switch (m.cmd) {
        case msp::MSP_RC:
            r = R_RC;
            break;
        case msp::MSP_STATUS:
            r = R_STATUS;
            break;
        case msp::MSP_BOXIDS:
            r = R_BOXIDS;
            break;
        case msp::MSP_RTC:
            r = R_RTC;
            break;
        case msp::MSP2_SET_TEXT:
            r = R_TEXT;
            break;
    }
    if (r >= 0)
        (m.error ? errors_ : replies_)[r]++;
    else
        logOther(m);
    if (!m.v2 && m.cmd == 2 && m.len >= 4 && memcmp(m.payload, "ULTR", 4) == 0 && !isUltra_) {
        // KISS Ultra: CamCandy sends MSP2_SET_TEXT tunnelled in v1 (cmd 255)
        isUltra_     = true;
        textFraming_ = TF_V2_IN_V1;
        textSent_    = 0;
        Serial.println("[fc] KISS Ultra detected - OSD text as v2-in-v1, 16 chars");
    }
    if (r == R_TEXT && !textLocked_) {
        textLocked_ = true;
        Serial.printf("[fc] OSD text answered (%s) using %s framing\n", m.error ? "ERR" : "ok",
                      textFraming_ == TF_V2 ? "MSPv2" : "v2-in-v1");
    }
    if (m.error)
        return;
    if (m.cmd == msp::MSP_RC) {
        const uint8_t n = (uint8_t)(m.len / 2);
        rc_.count       = n > RcState::MAX ? RcState::MAX : n;
        for (int i = 0; i < rc_.count; i++) {
            rc_.ch[i] = (uint16_t)m.payload[2 * i] | ((uint16_t)m.payload[2 * i + 1] << 8);
        }
        rc_.valid        = true;
        rc_.lastUpdateMs = millis();
    } else if (m.cmd == msp::MSP_RTC && m.len >= 9) {
        // year u16, month, day, hours, minutes, seconds, millis u16 (UTC). Empty until set
        time_.year    = (uint16_t)m.payload[0] | ((uint16_t)m.payload[1] << 8);
        time_.month   = m.payload[2];
        time_.day     = m.payload[3];
        time_.hours   = m.payload[4];
        time_.minutes = m.payload[5];
        time_.seconds = m.payload[6];
        time_.valid   = time_.year >= 2020;
    } else if (m.cmd == msp::MSP_BOXIDS) {
        // Permanent IDs in flag-bit order; BOXARM = 0, so its position is ARM's bit
        for (size_t i = 0; i < m.len && i < 32; i++) {
            if (m.payload[i] == 0) {
                armBit_ = (int8_t)i;
                break;
            }
        }
    } else if (m.cmd == msp::MSP_STATUS && armBit_ >= 0 && m.len >= 10) {
        const uint32_t flags = (uint32_t)m.payload[6] | ((uint32_t)m.payload[7] << 8) |
                               ((uint32_t)m.payload[8] << 16) | ((uint32_t)m.payload[9] << 24);
        armed_               = (flags >> armBit_) & 1;
        armedKnown_          = true;
    }
}

void BetaflightMsp::setOsdMessage(uint8_t slot, const char* text) {
    if (slot > 3)
        return;
    strncpy(textPend_[slot], text, sizeof(textPend_[slot]) - 1);
    textPend_[slot][sizeof(textPend_[slot]) - 1] = '\0';
    textDirty_ |= (uint8_t)(1u << slot);
}

void BetaflightMsp::sendQueuedText(uint32_t now) {
    if (!textDirty_ || now - lastTextMs_ < 100)
        return;
    uint8_t slot = textNext_;
    while (!(textDirty_ & (1u << slot)))
        slot = (uint8_t)((slot + 1) & 3);
    textDirty_ &= (uint8_t)~(1u << slot);
    textNext_  = (uint8_t)((slot + 1) & 3);
    lastTextMs_ = now;

    const char* text = textPend_[slot];
    uint8_t     len  = (uint8_t)strlen(text);
    const uint8_t maxLen = isUltra_ ? 16 : 30;  // KISS stores custom texts as 16 chars
    if (len > maxLen)
        len = maxLen;

    uint8_t payload[2 + 30];
    payload[0] = (uint8_t)(msp::MSP2TEXT_CUSTOM_MSG_0 + slot);
    payload[1] = len;
    memcpy(&payload[2], text, len);

    // No answer after a few sends: try the other framing (not on KISS Ultra, whose framing is
    // known; only once the FC talks MSP at all)
    if (!textLocked_ && !isUltra_ && replies_[R_RC] > 0 && ++textSent_ > 12) {
        textSent_    = 0;
        textFraming_ = (uint8_t)((textFraming_ + 1) % TF_COUNT);
        Serial.printf("[fc] OSD text unanswered - trying %s framing\n",
                      textFraming_ == TF_V2 ? "MSPv2" : "v2-in-v1");
    }

    auto f = msp::encodeV2(msp::MSP2_SET_TEXT, payload, 2 + len);
    if (textFraming_ == TF_V2_IN_V1) {
        // Same v2 frame minus "$X<", carried as the payload of v1 cmd 255
        f = msp::encodeV1(255, f.data() + 3, f.size() - 3);
    }
    serial_.write(f.data(), f.size());
    if (textSentTotal_ < 0xFFFF)
        textSentTotal_++;
}

void BetaflightMsp::diag(char* out, size_t n) const {
    // "ok" = replies seen, "ERR" = FC rejected it, "--" = no answer yet
    auto st = [this](Req r) { return replies_[r] ? "ok" : errors_[r] ? "ERR" : "--"; };
    // KISS Ultra displays MSP2_SET_TEXT without replying, so report it as sent, not missing
    const char* text = isUltra_ ? (textSentTotal_ ? "sent" : "--") : st(R_TEXT);
    snprintf(out, n, "MSP%s rc:%s status:%s boxids:%s%s rtc:%s text:%s(%s)%s",
             isUltra_ ? "/KISS" : "", st(R_RC), st(R_STATUS), st(R_BOXIDS),
             armBitGuessed_ ? "(bit0)" : "", st(R_RTC), text,
             textFraming_ == TF_V2 ? "v2" : "v1x", fcRequests_ ? " fc-polls-us" : "");
}
