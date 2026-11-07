# RustFox ESP32-C6 client (v0)

Firmware prototype for the Waveshare **ESP32-C6-Touch-AMOLED-2.16**. It speaks
the RustFox **Device WebSocket** from ADR-0056: JSON text frames on
`GET /api/device/v1/ws`, then draws a small fox face on the AMOLED.

This tree is the device client. It is **not** part of the RustFox monorepo.
Shipping it, or not shipping it, does **not** block the RustFox **v1.0.9**
tag. v0 is not a git tag.

The v0 path is Kan path B, narrowed for this chip:

- On-board capture and wake stay conceptual. Microphone pins are recorded and
  the codecs are not started.
- Speech-to-text is off the device. The ESP32-C6 on this board has **no
  PSRAM** (512 KB HP SRAM), so there is no on-board open-vocabulary STT.
- A turn is a `user_text` frame. v0 sends a canned line, the BOOT button, or
  a line typed into the serial monitor.
- Xiaozhi cloud, Opus, and binary audio frames are not implemented. TTS is
  later work.

## Hardware

| | |
| --- | --- |
| Board | [Waveshare ESP32-C6-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-c6-touch-amoled-2.16.htm) |
| SoC | ESP32-C6, 160 MHz RISC-V, Wi-Fi 6, BLE 5 |
| RAM | 512 KB HP SRAM, 16 KB LP SRAM, no PSRAM |
| Flash | 16 MB external NOR |
| Panel | 2.16" AMOLED, 480×480, QSPI |
| PMIC | AXP2101 at I2C `0x34` |
| Buttons | BOOT on GPIO9 |

The pin map, the AXP2101 sequence that powers the panel, and the QSPI init
commands are adapted from the MIT firmware
[78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32)
`main/boards/waveshare/esp32-c6-touch-amoled-2.16/`
(revision `a73e9a7e266e3196d1368ba46c30bee0d81f0a64`). See
`THIRD_PARTY_NOTICES.md`. Waveshare's own SDK samples are not copied: those
trees do not ship an SPDX header.

GPIO12 and GPIO13 are USB D−/D+ and are left alone.

## Bring-up

ESP-IDF **5.4 or newer**. A host build with **ESP-IDF 5.5.1** links
`build/rustfox_esp32_c6.bin` (245 KiB) for the 16 MB flash map. That
image was not flashed. Install the `esp32c6` tools, then:

```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py menuconfig
idf.py build flash monitor
```

`scripts/idf-build.sh` runs `set-target` and `build` when `IDF_PATH` (default
`$HOME/esp/esp-idf`) is already installed.

Menu path: **RustFox ESP32-C6**.

| Setting | Role |
| --- | --- |
| Wi-Fi SSID / password | Station join. Empty SSID boots the face and does not open the socket. An empty password joins an open network. |
| Device WebSocket URI | Full URI, including `/api/device/v1/ws`. Default `ws://192.168.1.10:8080/api/device/v1/ws`. |
| Device token transport | `Authorization: Bearer` (default) or a first text frame `{"type":"auth","token":"..."}`. |
| Device token | Portal **Settings → Devices** plaintext. Not the portal login token. Empty skips the socket. |
| device_id / room | Optional `device_hello` fields. Defaults: `esp32-c6-amoled-2.16` and an empty room. |
| Firmware version | Sent as `fw_version`. Default `0.1.0`. |
| Canned user_text | Stand-in for off-device ASR. Default `hello from the esp32-c6`. |
| Send canned on connect | One `user_text` after `device_hello` is acked, unless a typed line is already waiting. |
| BOOT button | GPIO9 sends the canned line. |
| Serial lines | A line typed into `idf.py monitor` is `user_text`. |
| JSON ping interval | Default 30 s, after auth. |
| QSPI panel | On by default. If the PMIC or the glass does not answer, the socket still runs. |

`sdkconfig` holds the SSID, password, and token. It is gitignored. Do not
commit it. The token is not written to the log.

On the RustFox side, `[device_ws] enabled = true`. The socket shares the
portal bind and port. Create a device under Settings → Devices and paste that
token here. Revoke or rotate it there; this firmware has no token store of
its own.

Host checks (no IDF, no hardware):

```bash
make -C tests/host test
```

## Frame table

Normative detail, error codes, and the connect sequence are in
[docs/device-ws.md](docs/device-ws.md).

| Direction | `type` | Fields | Role |
| --- | --- | --- | --- |
| Client → server | `auth` | `token` | Only when the Bearer header was not sent |
| Client → server | `user_text` | `text` | Required for a turn |
| Client → server | `ping` | — | Optional |
| Client → server | `device_hello` | `device_id`, optional `room`, `fw_version` | Optional. Sent after auth |
| Server → client | `assistant_text` | `text` | One or more replies |
| Server → client | `device_ack` | `status`, `device_id`, optional `room` | `authenticated`, `hello`, or `queued` |
| Server → client | `pong` | — | Reply to JSON `ping` |
| Server → client | `error` | `code`, `message` | Optional. `unauthorized` stops the client |

```json
{"type":"user_text","text":"turn on the lamp"}
```

```json
{"type":"assistant_text","text":"done"}
```

A wrong Bearer token fails the HTTP upgrade with **401**. The client then
stops reconnecting and holds a worried face.

## Fox face

`components/rf_face` is the on-device face: ears, eyes, a blink every 3.2 s
(held 140 ms), and a mouth that follows the emotion. The panel path blits a
**96×96** RGB565 sprite in 16-line stripes. There is no 480×480 framebuffer
(that alone is about 450 KB).

The serial log prints an ASCII fox when the emotion changes. Assistant text
is matched with a short substring list (English and a few Cantonese/Chinese
phrases). Anything else stays neutral. That map is a stub, not a sentiment
model.

The MIT widget [0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face)
is vendored under `third_party/lvgl_kawaii_face/` and **not linked**. Its
canvases are sized to the parent; a full-panel canvas does not fit next to
Wi-Fi. Emotion names overlap that widget (`neutral`, `happy`, `sad`,
`surprised`, `angry`, `sleepy`, `worried`). Xiaozhi's default emoji set for
this board is Twemoji, which is CC-BY-4.0 and is not vendored.

## What v0 does not do

- Block or retag RustFox v1.0.9
- Live inside the RustFox monorepo
- Xiaozhi cloud, Opus, or any binary Device WS audio
- Telegram voice capture or playback
- On-device open-vocabulary STT, wake-word models, or TTS
- Touch (the product page names CST9220; the MIT board file uses CST9217)

## License

MIT. Copyright (c) 2026 chinkan.ai. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.

## Bring-up blockers

These are open until the image is on a board. The host frame and face tests
do not need the glass.

1. **No PSRAM.** 512 KB HP SRAM is shared with Wi-Fi. v0 never allocates a
   full-frame buffer. Xiaozhi's display object sets a QSPI max transfer of
   `480×480×2`, which does not fit here; v0 caps the transfer at a 16-line
   stripe.
2. **Controller names disagree.** The Waveshare page calls the glass CO5300
   and the touch chip CST9220. The MIT Xiaozhi board file drives an SH8601
   QSPI command set (Apache-2.0 `espressif/esp_lcd_sh8601`) and CST9217.
   v0 follows that SH8601 command set and does not start touch.
3. **Address window.** That MIT init table programs `0x2A/0x2B` as a
   410×502 window (the 2.06" geometry) while `config.h` says 480×480. v0
   programs `0x0000`–`0x01DF` on both axes and keeps MADCTL `0x36 = 0xA0`.
   The bench has to confirm the window, the color order, and the MADCTL.
4. **Panel power.** The glass is powered by toggling AXP2101 ALDO3 the way
   the MIT board file does. If `0x34` does not ACK, the panel path is skipped
   and the socket still runs. Colors and brightness are unverified.
5. **Assistant text cap.** Frames larger than 8 KiB are dropped. The face and
   the log keep 1023 bytes of `text`. The server allows up to 256 KiB and
   16000 scalars of `user_text`; v0 manual lines are capped at 383 bytes.
6. **Not flashed.** ESP-IDF 5.5.1 `idf.py build` succeeds. Confirm on the
   board: Wi-Fi join, `device_ack`, one `user_text`, one `assistant_text`,
   and a blink on the glass.
