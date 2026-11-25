// User-editable config, persisted in NVS (edited via the Web UI)
#pragma once

#include <cstdint>

#include "control/Modes.h"

// What a given OSD "Custom Msg" slot displays
enum class OsdField : uint8_t {
    Off = 0,
    RecStatus,
    Battery,
    Mode,
    SdFree,
    TimeLeft,
    Resolution,
    Fps,
    Format,  // resolution + frame rate in one slot, e.g. "4K60"
    _Count,
};

const char* osdFieldName(OsdField f);

// Betaflight and KISS Ultra (firmware B57+) both talk MSP; KissNative is the older
// KISS serial protocol (no OSD text)
enum class FcType : uint8_t { Betaflight = 0, KissNative = 1, KissUltraMsp = 2, _Count };

// A paired camera kept for later. The active one is mirrored in camType/camMac
struct SavedCam {
    uint8_t type     = 0;   // as Settings::camType
    char    mac[18]  = "";  // "" = empty slot
    char    name[24] = "";  // model / BLE name from the scan, for the Web UI
};

struct Settings {
    // Bump on any layout change; old layouts need a COMPAT entry or they reset to defaults
    static constexpr uint16_t SCHEMA = 20;
    // Paired cameras remembered (NimBLE keeps 3 bonds by default, so 3 bonded cameras fit)
    static constexpr int MAX_CAMS = 3;

    uint32_t magic   = 0x53425247;  // 'SBRG'
    uint16_t version = SCHEMA;

    uint8_t opMode = (uint8_t)OpMode::RecordOnArm;

    // --- Wi-Fi config AP ---
    char    apSsid[32]        = "ShutterBridge";
    char    apPass[32]        = "shutterbridge";
    uint8_t webuiDisarmedOnly = 1;

    // BLE transmit power: 0 = low, 1 = medium, 2 = high (max)
    uint8_t bleTxPower = 0;

    // --- Camera selection ---
    uint8_t camType           = 0;   // 0 = DJI Osmo Series, 1 = DJI Action Series, 2 = GoPro
    char    camMac[18]        = "";  // bound via Web UI scan; "" falls back to name scan
    char    camNamePrefix[16] = "OsmoNano";

    // --- OSD: which field each of the 4 Custom Msg slots shows ---
    OsdField osdSlot[4] = {OsdField::RecStatus, OsdField::Battery, OsdField::Mode,
                           OsdField::SdFree};

    // --- Camera modes: AUX channel + active range per function (Modes.h) ---
    ModeRange modes[FUNC_COUNT] = {};

    // How the Shutter behaves in video mode (0 = Momentary/toggle, 1 = 2-position/hold)
    uint8_t shutterVideoMode = (uint8_t)ShutterVideoMode::TwoPos;

    // How the Camera Mode switch behaves
    uint8_t modeSwitchStyle = (uint8_t)ShutterVideoMode::TwoPos;

    // Delays (ms) applied to the Shutter's record commands
    uint16_t recordStartDelayMs = 0;
    uint16_t recordStopDelayMs  = 2000;

    // Sync the camera clock from FC GPS time (1 = on)
    uint8_t clockSync   = 1;
    int16_t tzOffsetMin = 0;

    // Wake a powered-off camera when the bridge boots (1 = on)
    uint8_t autoWake = 0;

    // Board variant (BoardVariant in Board.h): 0 = auto-detect from the chip
    uint8_t boardVariant = 0;

    // FC UART GPIOs; -1 = the board's default pin
    int8_t fcRxPin = -1;
    int8_t fcTxPin = -1;

    // Flight controller firmware (FcType): 0 = Betaflight, 1 = KISS native, 2 = KISS Ultra MSP
    uint8_t fcType = 0;

    // OSD warnings, 0 = off: blink when the camera battery / recording time left drops below
    uint8_t camBattWarnPct = 20;
    uint8_t recLeftWarnMin = 5;

    // --- Saved cameras: switch between them without re-pairing ---
    SavedCam cams[MAX_CAMS];
    // When the active camera is off for a while and another saved camera is on, switch to it
    uint8_t autoSwitch = 1;

    // Add or update a saved camera (by MAC). When full, replaces the first entry that is
    // not the active camera. Returns its index
    int  rememberCam(uint8_t type, const char* mac, const char* name);
    int  findCam(const char* mac) const;
    void forgetCam(int i);
    // Make saved camera i the active one (camType/camMac). Needs a reboot to take effect
    bool useCam(int i);

    void setDefaults();
    void load();  // from NVS (falls back to defaults if missing/incompatible)
    void save();  // to NVS
};

extern Settings g_settings;
