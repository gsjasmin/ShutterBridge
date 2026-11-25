#include "config/Settings.h"

#include <Arduino.h>
#include <Preferences.h>

#include <cstddef>
#include <cstring>

Settings g_settings;

static Preferences        prefs;
static constexpr uint32_t MAGIC = 0x53425247;

struct CompatVersion {
    uint16_t version;
    size_t   validBytes;
};
static const CompatVersion COMPAT[] = {
    {15, offsetof(Settings, autoWake)},
    {16, offsetof(Settings, boardVariant)},
    {17, offsetof(Settings, fcRxPin)},
    {18, offsetof(Settings, fcType)},
    {19, offsetof(Settings, cams)},
};

const char* osdFieldName(OsdField f) {
    switch (f) {
        case OsdField::Off:
            return "Off";
        case OsdField::RecStatus:
            return "Rec status";
        case OsdField::Battery:
            return "Battery %";
        case OsdField::Mode:
            return "Mode";
        case OsdField::SdFree:
            return "SD free";
        case OsdField::TimeLeft:
            return "Time left";
        case OsdField::Resolution:
            return "Resolution";
        case OsdField::Fps:
            return "FPS";
        case OsdField::Format:
            return "Format";
        default:
            return "?";
    }
}

void Settings::setDefaults() {
    *this = Settings();  // reset all members to their in-class defaults
    // Shutter on AUX1 high by default; in video that records while the switch is up (2-pos)
    modes[(int)Func::Shutter] = {/*aux=*/1, /*min=*/1800, /*max=*/2100};
    shutterVideoMode          = (uint8_t)ShutterVideoMode::TwoPos;
}

void Settings::load() {
    prefs.begin("shutter", /*readOnly=*/true);
    const size_t n        = prefs.getBytesLength("cfg");
    bool         ok       = false;
    bool         migrated = false;

    if (n >= offsetof(Settings, opMode) && n <= sizeof(Settings)) {
        uint8_t buf[sizeof(Settings)];
        prefs.getBytes("cfg", buf, n);
        uint32_t magic;
        uint16_t ver;
        memcpy(&magic, buf + offsetof(Settings, magic), sizeof(magic));
        memcpy(&ver, buf + offsetof(Settings, version), sizeof(ver));
        if (magic == MAGIC && ver == version && n == sizeof(Settings)) {
            memcpy(this, buf, n);
            ok = true;
        }
        for (const CompatVersion& c : COMPAT) {
            if (ok || magic != MAGIC || ver != c.version || n < c.validBytes)
                continue;
            const uint16_t current = version;
            setDefaults();  // fields added since that version keep their defaults
            memcpy(this, buf, c.validBytes);
            version = current;
            ok = migrated = true;
            Serial.printf("[cfg] migrated settings v%u -> v%u\n", ver, current);
            break;
        }
    }
    prefs.end();
    if (!ok)
        setDefaults();
    // Settings from before saved cameras: keep the bound camera in the list
    if (camMac[0] && findCam(camMac) < 0) {
        rememberCam(camType, camMac, "");
        migrated = true;
    }
    if (!ok || migrated)
        save();
}

void Settings::save() {
    prefs.begin("shutter", /*readOnly=*/false);
    prefs.putBytes("cfg", this, sizeof(*this));
    prefs.end();
}

static void copyStr(char* dst, size_t n, const char* src) {
    strncpy(dst, src ? src : "", n - 1);
    dst[n - 1] = '\0';
}

int Settings::findCam(const char* mac) const {
    if (!mac || !mac[0])
        return -1;
    for (int i = 0; i < MAX_CAMS; i++)
        if (cams[i].mac[0] && strcasecmp(cams[i].mac, mac) == 0)
            return i;
    return -1;
}

int Settings::rememberCam(uint8_t type, const char* mac, const char* name) {
    if (!mac || !mac[0])
        return -1;
    int i = findCam(mac);
    if (i < 0)
        for (int k = 0; k < MAX_CAMS && i < 0; k++)
            if (!cams[k].mac[0])
                i = k;
    if (i < 0)
        for (int k = 0; k < MAX_CAMS && i < 0; k++)
            if (strcasecmp(cams[k].mac, camMac) != 0)
                i = k;
    if (i < 0)
        i = 0;
    cams[i].type = type;
    copyStr(cams[i].mac, sizeof(cams[i].mac), mac);
    if (name && name[0])  // keep a known name when re-binding without one
        copyStr(cams[i].name, sizeof(cams[i].name), name);
    return i;
}

void Settings::forgetCam(int i) {
    if (i < 0 || i >= MAX_CAMS)
        return;
    cams[i] = SavedCam();
}

bool Settings::useCam(int i) {
    if (i < 0 || i >= MAX_CAMS || !cams[i].mac[0])
        return false;
    camType = cams[i].type;
    copyStr(camMac, sizeof(camMac), cams[i].mac);
    return true;
}
