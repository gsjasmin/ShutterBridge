<div align="center">

<img src="site/public/favicon.svg" alt="ShutterBridge" width="96" height="96" />

# ShutterBridge

**Your camera, on the OSD. Your switches, on the shutter.**

ESP32 firmware that links your flight controller and action camera - camera
status drawn straight on your goggles, and RC switches mapped to record, photo
and mode.

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-2563eb.svg?style=flat-square)](LICENSE)
[![Board](https://img.shields.io/badge/board-ESP32--S3%20%2F%20ESP32--C3-e7352c?style=flat-square&logo=espressif&logoColor=white)](https://shutterbridge.yarosfpv.com/docs/hardware)
[![Betaflight](https://img.shields.io/badge/Betaflight-2025.12%2B-32c766?style=flat-square)](https://betaflight.com)
[![Built with PlatformIO](https://img.shields.io/badge/built%20with-PlatformIO-f5822a?style=flat-square&logo=platformio&logoColor=white)](https://platformio.org)

[**⚡ Flash it now**](https://shutterbridge.yarosfpv.com/flash) &nbsp;·&nbsp;
[**📖 Documentation**](https://shutterbridge.yarosfpv.com/docs) &nbsp;·&nbsp;
[**🌐 Website**](https://shutterbridge.yarosfpv.com)

</div>

---

## What it does

ShutterBridge runs on a small ESP32-S3 or ESP32-C3 board wired to your flight controller. It
talks to your action camera over Bluetooth LE and to Betaflight over MSP, so you
get two things in flight:

- **Camera status on your OSD** - record state, battery, SD space, mode,
  resolution/FPS and recording timers, drawn right where you're looking.
- **RC switches on the shutter** - start recording when you arm, stop when you
  disarm, trigger photos, or switch modes from a channel. No touching the camera.

Everything is configured from a clean on-device Web UI - connect to the board's
Wi-Fi access point and open it in a browser. No app, no drivers.

## See it in action

<table>
  <tr>
    <td align="center" width="33%">
      <img src="https://github.com/user-attachments/assets/2451cfb0-8ea8-4793-b39c-9ace35b6bbc9" alt="Arm to record" width="100%" />
      <br /><sub><b>Arm to record</b></sub>
    </td>
    <td align="center" width="33%">
      <img src="https://github.com/user-attachments/assets/c90c3407-f401-44a4-91d5-208f4ecc4e10" alt="OSD in the goggles" width="100%" />
      <br /><sub><b>OSD in the goggles</b></sub>
    </td>
    <td align="center" width="33%">
      <picture>
        <source media="(prefers-color-scheme: dark)" srcset="site/public/gallery/ui-dark.webp" />
        <img src="site/public/gallery/ui-light.webp" alt="Web UI" width="100%" />
      </picture>
      <br /><sub><b>Web UI</b></sub>
    </td>
  </tr>
</table>

## Compatible cameras

| Feature                                           | DJI Osmo (Nano) | DJI Action / 360 | GoPro (HERO9+) |
| ------------------------------------------------- | :-------------: | :--------------: | :------------: |
| Connect + auto-reconnect                          |       ✅        |        ✅        |       ✅       |
| Pairing (accept PIN / bond on the camera)         |  not required   |     ✅ (PIN)     |   ✅ (bond)    |
| OSD telemetry (record state, battery, SD, timers) |       ✅        |        ✅        |       ✅       |
| Mode readout (video / photo)                      |       ✅        |        ✅        |       ❌       |
| Photo / video mode switch                         |       ✅        |        ✅        |       ❓       |
| Resolution / FPS readout                          |       ✅        |        ✅        |       ❓       |
| Start / stop recording (with optional delay)      |       ✅        |        ✅        |       ✅       |
| Take photo                                        |       ✅        |        ✅        |       ❓       |
| Preset switching (Video / Photo / Timelapse)      |       ❌        |        ❌        |       ❓       |
| Load preset by ID                                 |       ❌        |        ❌        |       ❓       |
| Clock sync from FC GPS time                       |       ❌        |        ❌        |       ❓       |
| Tested on real hardware                           |       ✅        |  ✅ (Action 4)   |  ✅ (HERO 12)  |

_❓ = supported by the backend but not yet verified on hardware._

## What you need

- **One of two boards** - **Waveshare ESP32-S3-Zero** (ESP32-S3FH4R2: 4 MB flash, 2 MB PSRAM, native USB) or **ESP32-C3 Super Mini** (ESP32-C3FH4: 4 MB flash, no PSRAM, USB-Serial/JTAG).
- **Betaflight 2025.12 or newer** on your flight controller - talks to the bridge over MSP and draws the OSD elements.
  - **Or a KISS Ultra (firmware B57+, MSP) or older KISS FC** (experimental) - see [KISS support](#kiss-support-experimental).
- **A supported action camera** - a DJI Osmo / Action or a GoPro HERO9 or newer.

## Getting started

The easiest path is the browser-based flasher - no toolchain required:

1. Plug the board into a data-capable USB-C port.
2. Open the [**web flasher**](https://shutterbridge.yarosfpv.com/flash) in Chrome or Edge, click **Connect** and pick the board's serial port when prompted.
3. It detects the board automatically; click **Install** and wait for it to finish.
4. Re-plug the board, connect to the `ShutterBridge` Wi-Fi access point (password `shutterbridge`), and open `http://10.0.0.1` to finish setup.

See the [full documentation](https://shutterbridge.yarosfpv.com/docs) for wiring, Betaflight OSD setup and per-camera notes.

## KISS support (experimental)

Two ways to connect a KISS flight controller. Pick one under **System > Flight controller** (the bridge reboots).

**KISS Ultra (MSP, B57+)** - recommended. KISS Ultra firmware B57 and newer speaks MSP and has OSD *Custom Text Messages 1-4*, so the bridge works as it does with Betaflight, OSD included.

1. Enable **MSP Protocol** on the UART wired to the bridge.
2. Enable and place **Custom Text Messages 1-4** in the OSD editor. They show OSD slots 0-3.
3. Check **System > Flight controller > Link** in the Web UI. It lists which MSP requests the FC answers (`ok`, `ERR` or `--`). If the FC does not answer `MSP_BOXIDS`, the bridge assumes ARM is flag bit 0 and shows `(bit0)`.

**KISS (KISS protocol)** - for older KISS firmware without MSP. Set the UART to **KissProtocol/OSD** in the KISS GUI. Switches and record-on-arm work (AUX1-7, armed flag via `GET_TELEMETRY` 0x20), but there is **no OSD text**.

Not available with either KISS mode: camera clock sync (no GPS time over the link) and the Betaflight bridge.

## Recording and OSD behaviour

- **Record on arm keeps recording while armed.** If the camera connects late, reconnects, or the bridge reboots mid-flight, recording is started again (every 5 s until the camera reports it). A clip started by hand on the camera keeps running until disarm.
- **Rec status** shows `CAM READY` when disarmed and idle. It blinks `NOT REC` while armed without a clip, `REC REJECTED` when the camera did not start after a record command, `NO SD!` when the camera reports no free space and no time left, and `DISCONNECTED` when the camera drops during a flight. If no camera was connected at any point in the current flight, the slot stays blank.
- **Warnings** (OSD tab): the camera battery and recording-time-left slots blink below the set thresholds (default 20 % and 5 min, 0 = off).
- **Format** slot: resolution and frame rate in one slot, e.g. `4K60`.

## Building from source

The firmware is a [PlatformIO](https://platformio.org) project targeting two boards:
the Waveshare ESP32-S3-Zero (env `esp32s3-zero`, the default) and the ESP32-C3 Super
Mini (env `esp32c3-supermini`). From the `firmware/` folder:

```bash
# Build and flash the firmware over USB (defaults to esp32s3-zero)
pio run -t upload

# For the ESP32-C3 Super Mini instead
pio run -e esp32c3-supermini -t upload
```

The Web UI in `firmware/data/` is gzipped and compiled into the firmware at build time
(`scripts/gzip_webui.py`), so `upload` alone ships it. No separate `uploadfs` step.

`pack-localhost-dev-firmware.sh [env]` bundles the build output into the site's local
web flasher for development (defaults to `esp32s3-zero`).

Planning to contribute? See [CONTRIBUTING.md](CONTRIBUTING.md) for the dev setup,
and how to submit changes.

## Repository layout

| Path        | What's inside                                                       |
| ----------- | ------------------------------------------------------------------- |
| `firmware/` | ESP32-S3 / ESP32-C3 firmware (PlatformIO) and the on-device Web UI. |
| `site/`     | The website, documentation and browser-based web flasher (Astro).   |

## License

ShutterBridge is free software, licensed under the **GNU General Public License
v3.0 or later**. See [`LICENSE`](LICENSE) for the full text.

Copyright © 2026 YarosFPV
