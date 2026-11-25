#include "fc/KissFc.h"

// Protocol reference: the KISS GUI (github.com/fedorcomander/kissfc-chrome-gui, js/protocol.js)
// and KISS_OSD. Multi-byte values are big-endian. Firmware >= 109 ends a frame with CRC8
// (poly 0xD5) over the payload; older firmware sends sum(payload)/len. Both are accepted.

static inline int16_t be16(const uint8_t* p) {
    return (int16_t)(((uint16_t)p[0] << 8) | p[1]);
}

// KISS stick/AUX value -1000..1000 -> 1000..2000 us
static inline uint16_t kissToUs(int16_t v) {
    int us = 1500 + v / 2;
    return (uint16_t)constrain(us, 800, 2200);
}

KissFc::KissFc(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin, uint32_t pollMs)
    : serial_(serial), baud_(baud), rxPin_(rxPin), txPin_(txPin), pollMs_(pollMs) {
}

void KissFc::setPins(int rxPin, int txPin) {
    rxPin_ = rxPin;
    txPin_ = txPin;
}

void KissFc::begin() {
    serial_.begin(baud_, SERIAL_8N1, rxPin_, txPin_);
    Serial.printf("[fc] KISS UART rx=%d tx=%d\n", rxPin_, txPin_);
}

void KissFc::poll() {
    const uint32_t now = millis();

    while (serial_.available()) {
        const int c = serial_.read();
        if (c < 0)
            break;
        feed((uint8_t)c);
    }

    if (now - lastReqMs_ >= pollMs_) {
        // A reply takes ~15 ms at 115200; a stuck parser is reset by the next request
        if (st_ != St::Start && now - lastReqMs_ > 200)
            st_ = St::Start;
        lastReqMs_ = now;
        serial_.write(CMD_GET_TELEMETRY);
    }
}

void KissFc::feed(uint8_t b) {
    switch (st_) {
        case St::Start:
            if (b == START_BYTE)
                st_ = St::Len;
            break;
        case St::Len:
            len_  = b;
            got_  = 0;
            crc8_ = 0;
            sum_  = 0;
            st_   = (len_ >= MIN_TELEMETRY_LEN) ? St::Payload : St::Start;
            break;
        case St::Payload:
            buf_[got_++] = b;
            sum_ += b;
            crc8_ ^= b;
            for (int i = 0; i < 8; i++)
                crc8_ = (crc8_ & 0x80) ? (uint8_t)((crc8_ << 1) ^ 0xD5) : (uint8_t)(crc8_ << 1);
            if (got_ >= len_)
                st_ = St::Crc;
            break;
        case St::Crc:
            st_ = St::Start;
            if (b == crc8_ || b == (uint8_t)(sum_ / len_)) {
                onTelemetry(buf_, len_);
            } else if (++crcErrors_ % 50 == 1) {
                Serial.printf("[fc] KISS CRC error (%lu)\n", (unsigned long)crcErrors_);
            }
            break;
    }
}

void KissFc::onTelemetry(const uint8_t* p, uint8_t len) {
    // Map to the Betaflight MSP_RC order so RcState::aux() works unchanged:
    // 0 roll, 1 pitch, 2 yaw, 3 throttle, 4.. AUX1..
    rc_.ch[0] = kissToUs(be16(p + 2));
    rc_.ch[1] = kissToUs(be16(p + 4));
    rc_.ch[2] = kissToUs(be16(p + 6));
    rc_.ch[3] = (uint16_t)constrain(1000 + be16(p + 0), 800, 2200);  // throttle 0..1000
    for (int i = 0; i < 4; i++)                                     // AUX1-4 at 8..15
        rc_.ch[4 + i] = kissToUs(be16(p + 8 + 2 * i));
    uint8_t count = 8;
    if (len >= 160) {  // AUX5-7 at 154..159 on newer firmware
        for (int i = 0; i < 3; i++)
            rc_.ch[8 + i] = kissToUs(be16(p + 154 + 2 * i));
        count = 11;
    }
    rc_.count        = count;
    rc_.valid        = true;
    rc_.lastUpdateMs = millis();

    frames_++;
    armed_       = p[16] != 0;
    armedKnown_  = true;
    lastFrameMs_ = rc_.lastUpdateMs;
}
