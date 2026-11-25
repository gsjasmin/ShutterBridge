#include <Arduino.h>
#include <NimBLEDevice.h>

#include "Board.h"
#include "SerialConsole.h"
#include "StatusLed.h"
#include "camera/AdvertProbe.h"
#include "camera/BleTxPower.h"
#include "camera/dji/DjiActionCamera.h"
#include "camera/dji/DjiOsmoCamera.h"
#include "camera/gopro/GoProCamera.h"
#include "config.h"
#include "config/Settings.h"
#include "control/ChannelActions.h"
#include "fc/BetaflightMsp.h"
#include "fc/KissFc.h"
#include "net/BfBridge.h"
#include "net/WebConfig.h"
#include "osd/OsdRenderer.h"

static BetaflightMsp  bfFc(FC_UART, FC_BAUD, FC_RX_PIN, FC_TX_PIN, FC_RC_POLL_MS);
static KissFc        kissFc(FC_UART, FC_BAUD, FC_RX_PIN, FC_TX_PIN, FC_RC_POLL_MS);
// Picked from g_settings.fcType at boot (changing it reboots)
static FlightController* fcPtr = &bfFc;
static ChannelActions actions;
static OsdRenderer*   osd = nullptr;
static WebConfig      web(g_settings);
static StatusLed      led(STATUS_LED_PIN, STATUS_LED_RGB, STATUS_LED_ACTIVE_LOW);
static BfBridge       bridge(FC_UART);
static SerialConsole  console(web);
static Camera*        cam = nullptr;

// Betaflight bridge mode: runtime-only (never saved), reboot to exit. While set,
// the camera task self-exits and the loop pumps MSP bytes instead
static volatile bool g_bridgeMode    = false;
static volatile bool s_cameraStopped = false;
static TaskHandle_t  s_cameraTask    = nullptr;

enum CamType : uint8_t { CAM_DJI_OSMO = 0, CAM_DJI_ACTION = 1, CAM_GOPRO = 2 };

static Camera* makeCamera(const Settings& s) {
    switch (s.camType) {
        case CAM_DJI_ACTION:
            return new DjiActionCamera(s.camMac);
        case CAM_GOPRO:
            return new GoProCamera(s.camMac, "GoPro");
        case CAM_DJI_OSMO:
        default:
            return new DjiOsmoCamera(s.camMac, s.camNamePrefix);
    }
}

static uint32_t lastStatus = 0;

static void applyActions() {
    actions.loadFromModes(g_settings.modes, (ShutterVideoMode)g_settings.shutterVideoMode,
                          (ShutterVideoMode)g_settings.modeSwitchStyle,
                          g_settings.recordStartDelayMs, g_settings.recordStopDelayMs,
                          (OpMode)g_settings.opMode);
}

static volatile bool g_armedNow = false;  // written by loop(), read by the camera task

// What a camera's BLE advert says about its power state. Powered-off DJI Osmo and GoPro
// cameras keep advertising (so they can be woken) with a power bit in the manufacturer
// data. A sleeping DJI Action 6 also keeps advertising and even accepts a connection, and
// its advert has no known power bit, so for the Action series an advert proves nothing
enum class AdvPower : uint8_t { On, Off, Unknown };

static AdvPower advertPower(uint8_t type, const AdvertInfo& adv) {
    const std::string& m = adv.mfr;
    switch (type) {
        case CAM_DJI_OSMO:
            if (m.size() >= 12 && (uint8_t)m[0] == 0xAA && (uint8_t)m[1] == 0x08)
                return ((uint8_t)m[11] & 0x01) ? AdvPower::Off : AdvPower::On;
            return AdvPower::Unknown;
        case CAM_GOPRO:
            if (m.size() >= 4 && (uint8_t)m[0] == 0xF2 && (uint8_t)m[1] == 0x02)
                return ((uint8_t)m[3] & 0x01) ? AdvPower::On : AdvPower::Off;
            return AdvPower::Unknown;
        default:
            return AdvPower::Unknown;
    }
}

// Survives the software reset a switch does: set after switching to a camera whose power
// state was a guess, so two sleeping cameras can't bounce the bridge back and forth.
// Cleared once any camera is awake
RTC_NOINIT_ATTR static uint32_t s_guessMagic;
RTC_NOINIT_ATTR static uint32_t s_guessedSwitch;
static constexpr uint32_t       kGuessMagic = 0x53574348;  // 'SWCH'

bool g_bootWakeAllowed = true;  // BootWake.h

static void switchTo(int i, const char* why) {
    const SavedCam& c = g_settings.cams[i];
    Serial.printf("[cam] saved camera %s (%s) %s - switching to it\n", c.mac,
                  c.name[0] ? c.name : "?", why);
    g_settings.useCam(i);
    g_settings.save();
    delay(200);
    ESP.restart();  // software reset: the next boot does not wake the camera
}

static bool haveOtherSavedCam() {
    for (int i = 0; i < Settings::MAX_CAMS; i++)
        if (g_settings.cams[i].mac[0] && strcasecmp(g_settings.cams[i].mac, g_settings.camMac))
            return true;
    return false;
}

// Which saved camera to move to, or -1 to stay with the active one.
//  - the active camera's advert says on            -> stay
//  - activeKnownPresent and it is advertising at all -> stay (boot: we may wake it)
//  - another camera says on                         -> that one
//  - another camera is advertising with no power bit (DJI Action) -> that one, at most once
//    in a row (s_guessedSwitch), and only when the active camera is off or gone
static int pickSavedCamera(bool stayIfActiveAdvertising, bool allowGuess) {
    static uint8_t loggedAdv = 0;  // bitmask: advert bytes logged once per camera
    AdvertInfo     adv;
    if (g_settings.camMac[0] && probeAdvert(g_settings.camMac, "", 1500, adv)) {
        const AdvPower p = advertPower(g_settings.camType, adv);
        if (p == AdvPower::On || (stayIfActiveAdvertising && p == AdvPower::Unknown))
            return -1;
    }
    int guess = -1;
    for (int i = 0; i < Settings::MAX_CAMS; i++) {
        const SavedCam& c = g_settings.cams[i];
        if (!c.mac[0] || !strcasecmp(c.mac, g_settings.camMac))
            continue;
        if (!probeAdvert(c.mac, "", 1500, adv))
            continue;
        if (!(loggedAdv & (1u << i))) {  // to learn power bits of models not decoded yet
            loggedAdv |= 1u << i;
            Serial.printf("[cam] advert %s mfr:", c.mac);
            for (unsigned char b : adv.mfr)
                Serial.printf(" %02x", b);
            Serial.println();
        }
        const AdvPower p = advertPower(c.type, adv);
        if (p == AdvPower::On)
            return i;
        if (p == AdvPower::Unknown && guess < 0)
            guess = i;
    }
    if (guess >= 0 && allowGuess && !s_guessedSwitch) {
        s_guessedSwitch = 1;
        return guess;
    }
    return -1;
}

// Boot: before the active camera's backend starts (and wakes it), look whether another
// saved camera is already switched on - the one the user brought today - and go to it
static void bootPreselect() {
    if (s_guessMagic != kGuessMagic) {  // cold boot: RTC memory holds garbage
        s_guessMagic    = kGuessMagic;
        s_guessedSwitch = 0;
    }
    if (!g_settings.autoSwitch || !haveOtherSavedCam())
        return;
    NimBLEDevice::init("ShutterBridge");  // the backend's own init() is then a no-op
    // When the active camera is about to be woken, only a camera that surely is on wins
    // over it; a DJI Action that is merely advertising may just be asleep
    const bool willWake = g_settings.autoWake && g_bootWakeAllowed;
    const int  i = pickSavedCamera(/*stayIfActiveAdvertising=*/true, /*allowGuess=*/!willWake);
    if (i >= 0)
        switchTo(i, "is on at boot");
}

// Saved cameras: the backend is fixed at boot, so switching means making another saved
// camera active and rebooting. Only while disarmed, and only after the active camera has
// not been awake for a while (a bare BLE link does not count)
static void maybeSwitchCamera() {
    static uint32_t    lastOkMs    = 0;  // active camera awake
    static uint32_t    lastProbeMs = 0;
    constexpr uint32_t kGraceMs    = 20000;
    constexpr uint32_t kBootMs     = 30000;  // room to accept a PIN right after binding
    constexpr uint32_t kProbeMs    = 6000;

    const uint32_t now = millis();
    if (cam->isAwake(now))
        s_guessedSwitch = 0;
    if (!g_settings.autoSwitch || g_armedNow || cam->isAwake(now)) {
        lastOkMs = now;
        return;
    }
    if (now < kBootMs || now - lastOkMs < kGraceMs || now - lastProbeMs < kProbeMs)
        return;
    lastProbeMs = now;
    if (!haveOtherSavedCam())
        return;

    const int i = pickSavedCamera(/*stayIfActiveAdvertising=*/false, /*allowGuess=*/true);
    if (i >= 0)
        switchTo(i, "is available");
}

// The camera's BLE stack uses blocking calls (connect, service discovery)
static void cameraTask(void*) {
    bootPreselect();  // may reboot into another saved camera
    cam->begin();
    for (;;) {
        // Self-exit between polls (never mid-BLE-call) so main can deinit NimBLE
        if (g_bridgeMode) {
            s_cameraStopped = true;
            s_cameraTask    = nullptr;
            vTaskDelete(nullptr);
        }
        cam->poll();
        maybeSwitchCamera();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void enterBridgeMode() {
    Serial.println("[bridge] entering Betaflight bridge mode (reboot to exit)");
    g_bridgeMode = true;
    web.setBridgeActive(true);  // so /status.json reports it across a page reload

    // Wait for the task to self-exit, then release BLE
    const uint32_t deadline = millis() + 2000;
    while (!s_cameraStopped && millis() < deadline)
        delay(10);
    if (s_cameraStopped && NimBLEDevice::isInitialized())
        NimBLEDevice::deinit(true);

    bridge.begin();
}

void setup() {
    Serial.setRxBufferSize(2048);
    Serial.begin(CONSOLE_BAUD);
    delay(300);
    Serial.println("\nShutterBridge starting");

    g_settings.load();
    Serial.printf("[cfg] v=%u opMode=%u disarmedOnly=%u ssid=\"%s\"\n", g_settings.version,
                  g_settings.opMode, g_settings.webuiDisarmedOnly, g_settings.apSsid);
    boardInit(g_settings.boardVariant);
    // Wake the camera only on a real power-up, never after a software reset
    g_bootWakeAllowed = esp_reset_reason() != ESP_RST_SW;
    Serial.printf("[cam] boot wake %s\n", g_bootWakeAllowed ? "allowed" : "skipped (soft reset)");
    cam = makeCamera(g_settings);
    // Start the BLE camera on its own core-0 task before the rest so NimBLE is up early
    xTaskCreatePinnedToCore(cameraTask, "camera", 8192, nullptr, 1, &s_cameraTask, 0);
    // KISS Ultra B57+ speaks MSP like Betaflight; only the old KISS protocol needs KissFc
    fcPtr = (g_settings.fcType == (uint8_t)FcType::KissNative) ? (FlightController*)&kissFc
                                                               : (FlightController*)&bfFc;
    static const char* const FC_NAMES[] = {"Betaflight", "KISS (native)", "KISS Ultra (MSP)"};
    Serial.printf("[cfg] fc=%s\n", FC_NAMES[g_settings.fcType % 3]);
    osd = new OsdRenderer(*fcPtr, g_settings, OSD_UPDATE_MS, OSD_REFRESH_MS);
    int rxPin, txPin;
    boardFcPins(g_settings.fcRxPin, g_settings.fcTxPin, rxPin, txPin);
    fcPtr->setPins(rxPin, txPin);
    fcPtr->begin();
    applyActions();
    web.attachStatus(&cam->status(), &fcPtr->rc());
    web.attachFc(fcPtr);
    web.attachOsd(osd);
    web.attachCamera(cam);
    web.begin();
    led.begin(boardLedGrb());
}

void loop() {
    console.poll();

    // Bridge mode: just the MSP pump, the AP, and the LED
    if (g_bridgeMode) {
        web.poll(/*armed=*/false);  // armed=false keeps the AP up (reboot page)
        bridge.loop();
        led.showBridge(bridge.clientConnected());
        return;
    }
    if (web.takeBridgeRequest()) {
        enterBridgeMode();
        return;
    }

    fcPtr->poll();

    const RcState& rc    = fcPtr->rc();
    const uint32_t now   = millis();
    const bool     armed = fcPtr->armedKnown() && fcPtr->armed();  // from the FC (MSP or KISS)
    g_armedNow           = armed;

    actions.update(rc, *cam, armed);

    const CameraStatus s = cam->status();  // snapshot (written by the camera task)
    const bool online = cam->isConnected() && s.lastUpdateMs != 0 && (now - s.lastUpdateMs < 2500);
    osd->update(s, online, armed, actions.recRejected());

    web.poll(armed);
    if (web.takeDirty()) {
        applyActions();
        applyBleTxPower();
    }

    // Sync the GoPro clock from the FC's GPS-derived UTC, once per connection
    static bool clockSynced = false;
    if (!cam->isConnected()) {
        clockSynced = false;
    } else if (!clockSynced && g_settings.clockSync && g_settings.camType == CAM_GOPRO &&
               fcPtr->time().valid) {
        const FcTime&     t = fcPtr->time();
        Camera::WallClock wc{t.year, t.month, t.day, t.hours, t.minutes, t.seconds};
        if (cam->setDateTime(wc, g_settings.tzOffsetMin))
            clockSynced = true;
    }

    led.update(online, s.isRecording(), web.clientConnected());

    if (now - lastStatus >= 1000) {
        lastStatus = now;
        Serial.printf("cam:%s %s %02lu:%02lu batt %u%%  |  AUX1=%u armed:%u rc:%s\n",
                      online ? "on" : "off", toString(s.recState),
                      (unsigned long)(s.recElapsedS / 60), (unsigned long)(s.recElapsedS % 60),
                      s.batteryPct, rc.aux(1), armed, rc.valid ? "ok" : "--");
        static uint8_t diagTick = 0;
        if (++diagTick % 5 == 0) {
            char d[96];
            fcPtr->diag(d, sizeof(d));
            Serial.printf("[fc] %s\n", d);
            // Memory: free heap now / lowest ever / largest allocatable block, and the
            // fewest free stack bytes the camera and loop tasks have had
            Serial.printf("[mem] heap %u min %u maxblk %u | stack cam %u loop %u\n",
                          (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
                          (unsigned)ESP.getMaxAllocHeap(),
                          s_cameraTask ? (unsigned)uxTaskGetStackHighWaterMark(s_cameraTask) : 0,
                          (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        }
    }

#if CONFIG_FREERTOS_UNICORE
    // Single-core chips (ESP32-C3): loop() shares the only core with the camera task and
    // the idle task - block for a tick so neither is starved
    vTaskDelay(1);
#endif
}
