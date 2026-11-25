// KISS FC / KISS Ultra over the native KISS serial protocol.
//
// Set the FC UART to "KissProtocol/OSD" in the KISS GUI. The bridge polls GET_TELEMETRY
// (0x20) and reads sticks, AUX1-7 and the armed flag from the reply. KISS has no RTC
// readout and no custom OSD text, so time() stays invalid and setOsdMessage() is a no-op.
#pragma once

#include <Arduino.h>

#include "fc/FlightController.h"

class KissFc : public FlightController {
   public:
    KissFc(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin, uint32_t pollMs = 50);

    void           setPins(int rxPin, int txPin) override;  // before begin()
    void           begin() override;
    void           poll() override;
    const RcState& rc() const override {
        return rc_;
    }
    const FcTime& time() const override {
        return time_;
    }
    bool armed() const override {
        return armed_;
    }
    bool armedKnown() const override {
        return armedKnown_ && (millis() - lastFrameMs_) < 1000;
    }
    void setOsdMessage(uint8_t slot, const char* text) override {
    }
    void diag(char* out, size_t n) const override {
        snprintf(out, n, "KISS frames:%lu crcErr:%lu", (unsigned long)frames_,
                 (unsigned long)crcErrors_);
    }

    // Byte-level parser, public so it can be fed from tests
    void feed(uint8_t b);

   private:
    static constexpr uint8_t CMD_GET_TELEMETRY = 0x20;
    static constexpr uint8_t START_BYTE        = 0x05;
    static constexpr uint8_t MIN_TELEMETRY_LEN = 66;  // up to and including the mode byte

    void onTelemetry(const uint8_t* p, uint8_t len);

    HardwareSerial& serial_;
    uint32_t        baud_;
    int             rxPin_;
    int             txPin_;
    uint32_t        pollMs_;

    // Parser: [0x05][len][payload x len][crc]
    enum class St : uint8_t { Start, Len, Payload, Crc };
    St       st_       = St::Start;
    uint8_t  len_      = 0;
    uint8_t  got_      = 0;
    uint8_t  crc8_     = 0;
    uint32_t sum_      = 0;
    uint8_t  buf_[255] = {0};

    RcState  rc_;
    FcTime   time_;  // never valid on KISS
    bool     armed_       = false;
    bool     armedKnown_  = false;
    uint32_t lastReqMs_   = 0;
    uint32_t lastFrameMs_ = 0;
    uint32_t crcErrors_   = 0;
    uint32_t frames_      = 0;
};
