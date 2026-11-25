#pragma once

#include <Arduino.h>

#include "fc/FlightController.h"
#include "msp.h"

class BetaflightMsp : public FlightController {
   public:
    BetaflightMsp(HardwareSerial& serial, uint32_t baud, int rxPin, int txPin,
                  uint32_t rcPollMs = 50);

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
        return armedKnown_;
    }
    void setOsdMessage(uint8_t slot, const char* text) override;
    void diag(char* out, size_t n) const override;

   private:
    // Reply counters per request, so a bench log shows what the FC answers
    // (KISS Ultra's MSP is a subset of Betaflight's)
    enum Req : uint8_t { R_RC, R_STATUS, R_BOXIDS, R_RTC, R_TEXT, R_COUNT };
    uint32_t replies_[R_COUNT] = {0};
    uint32_t errors_[R_COUNT]  = {0};
    uint8_t  boxIdsTries_      = 0;
    bool     armBitGuessed_    = false;

    // OSD text framing. Betaflight takes MSP2_SET_TEXT as a native "$X" frame; some FCs only
    // parse "$M" frames, so after unanswered sends fall back to v2 tunnelled in v1 (cmd 255)
    enum TextFraming : uint8_t { TF_V2, TF_V2_IN_V1, TF_COUNT };
    uint8_t  textFraming_   = TF_V2;
    bool     textLocked_    = false;  // FC answered (ok or error) in this framing
    uint16_t textSent_      = 0;      // sends in the current framing without an answer
    bool     identSent_     = false;  // one-shot API/variant/version queries for the log
    uint8_t  otherLogged_   = 0;      // unexpected replies logged so far (capped)
    uint16_t fcRequests_    = 0;      // "$M<"/"$X<" frames *from* the FC (it polling us)
    uint8_t  reqLogged_     = 0;
    // Texts are queued and sent one frame at a time: KISS drops back-to-back bursts
    char     textPend_[4][31] = {};
    uint8_t  textDirty_       = 0;  // bit per slot
    uint8_t  textNext_        = 0;  // round-robin start
    uint32_t lastTextMs_      = 0;
    bool     isUltra_         = false;  // FC_VARIANT "ULTR": CamCandy-style framing, 16 chars
    void     sendQueuedText(uint32_t now);
    uint16_t textSentTotal_ = 0;  // KISS shows the text but never acknowledges it
    void     sendIdent();
    void     logOther(const msp::Message& m);
    void     logFrame(const msp::Message& m);

    void requestRc();
    void requestRtc();
    void requestStatus();
    void requestBoxIds();
    void onMessage(const msp::Message& m);

    HardwareSerial& serial_;
    uint32_t        baud_;
    int             rxPin_;
    int             txPin_;
    uint32_t        rcPollMs_;

    msp::Parser parser_;
    RcState     rc_;
    FcTime      time_;
    bool        armed_        = false;
    bool        armedKnown_   = false;
    int8_t      armBit_       = -1;  // ARM's bit in flightModeFlags; -1 until MSP_BOXIDS parsed
    uint32_t    lastReqMs_    = 0;
    uint32_t    lastRtcReqMs_ = 0;
    uint32_t    lastStatusMs_ = 0;
    uint32_t    lastBoxIdsMs_ = 0;
};
