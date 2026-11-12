# RustFox ESP32-C6 firmware for the Waveshare ESP32-C6-Touch-AMOLED-2.16

ESP-IDF firmware for the [Waveshare ESP32-C6-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-c6-touch-amoled-2.16.htm). It runs on the ESP32-C6 and drives the board's 2.16-inch 480×480 AMOLED. This repository is the [RustFox](https://github.com/chinkan/RustFox) Device WebSocket client: it joins Wi-Fi, exchanges JSON text frames on `GET /api/device/v1/ws`, draws a full-screen procedural fox on the panel, and uploads a press-and-hold recording for the host to transcribe.

## What this client does

[RustFox](https://github.com/chinkan/RustFox) is the self-hosted assistant. This firmware lives in this repository. It is not part of the RustFox monorepo.

With a Wi-Fi network and a device token, the board opens the Device WebSocket on your RustFox instance, sends text, and shows the assistant reply on the fox face. You send text with a canned line, the BOOT button, or a line typed in the serial monitor. Hold anywhere on the glass to record; release uploads a 16 kHz mono WAV, and the board plays the 16 kHz mono PCM reply. Speech-to-text runs on the RustFox host, not on the device.

## Features

- Wi-Fi station mode.
- JSON text frames on `GET /api/device/v1/ws`.
- Device token in an `Authorization: Bearer` header, or in a first `auth` text frame.
- `device_hello`, `user_text`, JSON `ping`, and `assistant_text`.
- Canned `user_text`, the BOOT button on GPIO9, and lines from `idf.py monitor`.
- A full-screen procedural fox, drawn in 16-line stripes. Ears, head, eyes, nose, and mouth ease between idle, listening, thinking, speaking, and happy.
- QSPI AMOLED bring-up through the AXP2101 power-management chip.
- Touch on the shared I2C bus. The firmware probes CST9220 and CST9217 and uses whichever answers.
- Press-and-hold to talk. ES7210 capture and ES8311 playback on I2S. The clip is written to a flash partition, uploaded as one WAV, and the PCM reply is streamed to the speaker.
- Host tests for the JSON frames, the fox face, and the press-and-hold timing (`make -C tests/host test`).

## Hardware

| | |
| --- | --- |
| Board | [Waveshare ESP32-C6-Touch-AMOLED-2.16](https://www.waveshare.com/esp32-c6-touch-amoled-2.16.htm) |
| SoC | ESP32-C6, 160 MHz RISC-V, Wi-Fi 6, BLE 5 |
| RAM | 512 KB HP SRAM, 16 KB LP SRAM, no PSRAM |
| Flash | 16 MB external NOR |
| Panel | 2.16" AMOLED, 480×480, QSPI |
| PMIC | AXP2101 at I2C `0x34` |
| Touch | CST9220 or CST9217 at I2C `0x5A`, reset GPIO11, interrupt GPIO5 |
| Audio | ES7210 capture and ES8311 playback, I2S MCLK 19, BCLK 20, WS 22, DIN 21, DOUT 23 |
| Buttons | BOOT on GPIO9 |

Pin assignments are in `main/board_pins.h`. GPIO12 and GPIO13 are USB D− and D+ and are left unused. The partition table in `partitions.csv` gives the factory app 4 MB of the 16 MB flash, then an `audio` partition that holds one press-and-hold clip.

The pin map, the AXP2101 sequence that powers the panel, and the QSPI init commands are adapted from the MIT firmware [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) `main/boards/waveshare/esp32-c6-touch-amoled-2.16/` (revision `a73e9a7e266e3196d1368ba46c30bee0d81f0a64`). See `THIRD_PARTY_NOTICES.md`.

### Panel and memory

**No PSRAM.** 512 KB of HP SRAM is shared with Wi-Fi. The fox is blitted in 16-line stripes. A full 480×480 RGB565 framebuffer is about 450 KB, and this firmware does not allocate one. The QSPI bus max transfer is one stripe, `480 × 16 × 2` bytes.

**Controller names.** The Waveshare product page calls the glass CO5300 and the touch chip CST9220. This firmware initializes the panel with the SH8601 QSPI command set from `espressif/esp_lcd_sh8601` (Apache-2.0), the same command set as the Xiaozhi board file. That board file names the touch chip CST9217. Both parts use I2C `0x5A`. At boot the firmware reads the chip id and keeps the part that answers.

**Address window.** Column and page commands `0x2A` and `0x2B` are programmed as `0x0000`–`0x01DF` on both axes (480 pixels). Command `0x36` (MADCTL) is `0xA0`. The panel config uses RGB element order. The Xiaozhi init table this sequence comes from programs a 410×502 window (2.06-inch geometry) while its `config.h` names 480×480. This firmware uses the 480×480 window.

**Panel power.** The glass is powered by toggling AXP2101 ALDO3 at I2C `0x34`, using the same sequence as the Xiaozhi board file. If `0x34` does not acknowledge, panel setup stops and the WebSocket client still runs.

## Prerequisites

- This Waveshare board, and a RustFox instance the board can reach.
- ESP-IDF 5.4 or newer, with the `esp32c6` tools installed. `main/idf_component.yml` requires `idf >= 5.4.0`. A host build with ESP-IDF 5.5.1 links `build/rustfox_esp32_c6.bin` (245 KiB) for the 16 MB flash map in `sdkconfig.defaults`.
- A C compiler and `make`, if you want the host tests. Those tests do not need ESP-IDF or the board.

`scripts/idf-build.sh` looks for ESP-IDF at `$IDF_PATH`, or at `$HOME/esp/esp-idf` when `IDF_PATH` is unset. If `export.sh` is missing there, the script tells you to clone a 5.5 release and run `./install.sh esp32c6`.

## Configure, build, flash, and monitor

### Enable Device WebSocket on RustFox

In the RustFox configuration, set:

```toml
[device_ws]
enabled = true
```

The socket uses the same bind address and port as the portal. The option defaults to off. Create a device under **Settings → Devices** and copy the plaintext device token. The portal shows that token once, when you create or rotate it. Revoke or rotate the token there. The copy this firmware uses lives in `sdkconfig` on your build machine.

Use `ws://` on loopback, Tailscale, or a trusted LAN. Use `wss://` when TLS terminates at a reverse proxy in front of RustFox. RustFox does not terminate TLS. A bare public WebSocket is not recommended. `sdkconfig.defaults` sets `CONFIG_MBEDTLS_CERTIFICATE_BUNDLE` so a `wss://` URI can use the bundled CA store.

### Configure the firmware

From the root of this repository:

```bash
. $HOME/esp/esp-idf/export.sh
idf.py set-target esp32c6
idf.py menuconfig
```

When ESP-IDF is installed somewhere else, set `IDF_PATH` and source `$IDF_PATH/export.sh` instead.

Menu path: **RustFox ESP32-C6**.

| Setting | Role |
| --- | --- |
| Wi-Fi SSID / password | Station join. An empty SSID boots the face and skips the network. An empty password joins an open network. |
| Device WebSocket URI | Full URI, including `/api/device/v1/ws`. Default `ws://192.168.1.10:8080/api/device/v1/ws`. |
| Device token transport | `Authorization: Bearer` (default) or a first text frame `{"type":"auth","token":"..."}`. |
| Device token | Plaintext from portal **Settings → Devices**. This is the device token, not the portal login token. An empty token skips the socket. |
| device_id / room | Optional `device_hello` fields. Defaults: `esp32-c6-amoled-2.16` and an empty room. |
| Firmware version | Sent as `fw_version`. Default `0.1.0`. |
| Canned user_text | Default `hello from the esp32-c6`. Also sent by the BOOT button. |
| Send canned on connect | On by default. One `user_text` after `device_hello` is acknowledged, unless a typed line is already waiting. |
| BOOT button | On by default. GPIO9 sends the canned line. |
| Serial lines | On by default. A line typed into `idf.py monitor` is `user_text`. |
| JSON ping interval | Default 30000 ms (30 seconds), after authentication. Range 5000–120000 ms. |
| QSPI panel | On by default. If the PMIC or the panel does not answer, the socket still runs. The fox is drawn in 16-line stripes. |
| Touch | On by default. Probes CST9220 and CST9217. Press anywhere on the glass and hold to record. |
| Press-and-hold talk | On by default, with touch. Release uploads the clip. A hold under about 0.3 s is dropped. A hold stops at about 15 s and sends what was captured. |

`sdkconfig` holds the SSID, the password, and the device token. `.gitignore` lists `sdkconfig` and `sdkconfig.old`. Do not commit them. The log records the token length and leaves the token itself out.

Put your RustFox host in the URI and keep the path `/api/device/v1/ws`. The default host `192.168.1.10` and port `8080` are only a starting point.

### Build, flash, and monitor

```bash
idf.py build flash monitor
```

Pass `-p` with the board's serial device if `idf.py` does not select it. `scripts/idf-build.sh` runs `idf.py set-target esp32c6` and `idf.py build` only. It does not flash or open the monitor.

After the board joins Wi-Fi, the log shows the socket connecting, a `device_ack`, and `device_hello sent`. `assistant_text` is printed when RustFox replies. The fox blinks on the glass when the panel comes up.

### Send text

- Press BOOT (GPIO9) to send the canned line.
- Type a line in the `idf.py monitor` console and press Enter to send that line as `user_text`.

A line longer than 383 bytes is dropped. If a typed line is already waiting when `device_hello` is acknowledged, the canned line is skipped.

### Run the host tests

```bash
make -C tests/host test
```

The tests compile `components/rf_frames`, `components/rf_face`, and `components/rf_talk` on the host. GitHub Actions runs the same command on every push and pull request.

## Protocol

Full frame fields, error codes, and the connect sequence are in [docs/device-ws.md](docs/device-ws.md). Server behavior lives in `src/device_ws.rs` in the [RustFox](https://github.com/chinkan/RustFox) repository.

Each WebSocket message is one JSON text frame. This client ignores binary frames. The server rejects those with `error` / `bad_frame`.

| Direction | `type` | Fields | Role |
| --- | --- | --- | --- |
| Client → server | `auth` | `token` | Only when the Bearer header was not sent |
| Client → server | `user_text` | `text` | Starts a turn |
| Client → server | `ping` | — | Optional keepalive |
| Client → server | `device_hello` | `device_id`, optional `room`, `fw_version` | Optional. Sent after authentication |
| Server → client | `assistant_text` | `text` | One or more replies |
| Server → client | `assistant_audio` | `id`, `url`, `sample_rate`, `channels`, `encoding` | PCM is ready to download |
| Server → client | `device_ack` | `status`, `device_id`, optional `room` | `authenticated`, `hello`, or `queued` |
| Server → client | `pong` | — | Reply to JSON `ping` |
| Server → client | `error` | `code`, `message` | Optional. `unauthorized` stops the client |

```json
{"type":"user_text","text":"turn on the lamp"}
```

```json
{"type":"assistant_text","text":"done"}
```

```json
{"type":"assistant_audio","id":"clip-1","url":"/api/device/v1/audio/clip-1","sample_rate":16000,"channels":1,"encoding":"s16le"}
```

What the client sends:

1. Connect.
2. With the Bearer header (the default), wait for `device_ack` / `authenticated`. With first-frame auth, send `auth`, then wait for the same acknowledgement.
3. Send `device_hello` (`device_id`, optional `room`, `fw_version`, default `0.1.0`).
4. On `device_ack` / `hello`, send one canned `user_text` when that option is on and no typed line is waiting.
5. BOOT (GPIO9) or a line from the serial monitor sends another `user_text`.
6. Log `assistant_text` and map a few substrings onto the fox face.
7. A press on the glass records. Release uploads one 16 kHz mono WAV to `POST /api/device/v1/audio` with the same Bearer token. `assistant_audio` points at `GET /api/device/v1/audio/{id}`, and the board plays that 16 kHz mono PCM. Holding again while the reply is playing stops it and starts a new recording.

JSON `ping` is separate from WebSocket protocol ping and pong. This client sends a JSON `ping` on the menu interval after authentication. The default is 30 seconds.

A wrong or revoked Bearer token fails the HTTP upgrade with status **401**. There is no JSON body on that failure. The client stops reconnecting and holds a worried face. An `error` frame with code `unauthorized` does the same. Other error codes and the face each one selects are listed in [docs/device-ws.md](docs/device-ws.md).

## Fox face

`components/rf_face` draws the on-device face across the full 480×480 panel: ears, head, eyes, nose, and mouth. Idle blinks, sways the ears, and breathes. Listening, thinking, speaking, and the other emotions ease into place. The panel code paints 16-line stripes. There is no 480×480 framebuffer and no sprite atlas.

Emotions are `neutral`, `happy`, `sad`, `surprised`, `worried`, `sleepy`, `thinking`, and `angry`. Connection loss and `unauthorized` draw the worried overlay. The serial log prints an ASCII fox when the emotion changes. Assistant text is matched with a short substring list (English and a few Cantonese and Chinese phrases). Any other text stays neutral. A hold shows a listening cue. At the 15 second cap the cue changes, then the clip is uploaded. While the reply plays, the mouth follows the speaking cue.

The MIT widget [0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face) is vendored under `third_party/lvgl_kawaii_face/` and is not linked. Its canvases are sized to the parent, and a full-panel canvas does not fit beside Wi-Fi. Emotion names overlap that widget where both define them (`neutral`, `happy`, `sad`, `surprised`, `angry`, `sleepy`, `worried`). The Xiaozhi sample for this board uses Twemoji, which is CC-BY-4.0. Twemoji is not vendored here.

## Current limits

- Speech-to-text runs on the RustFox host, not on the device. This ESP32-C6 board has no PSRAM (512 KB HP SRAM), so the firmware has no on-device speech-to-text and no wake-word model.
- The microphone and speaker are the ES7210 and ES8311 on the I2S pins in `main/board_pins.h`. Capture is 16 kHz mono, streamed into the `audio` flash partition. Playback is 16 kHz mono PCM. This firmware does not run a wake-word model.
- Display updates are 16-line stripes. The fox is drawn to fill the 480×480 panel. A full-panel framebuffer is not allocated.
- Incoming text frames larger than 8 KiB are dropped. The log and the face keep 1023 bytes of `text`. RustFox accepts a frame up to 256 KiB and a `user_text` of up to 16000 Unicode scalars. A monitor line longer than 383 bytes is dropped.
- This client sends JSON text frames on the Device WebSocket. It ignores binary frames. Audio upload and download use HTTP on the same host and port as that socket.

## License

MIT. Copyright (c) 2026 chinkan.ai. See `LICENSE` and `THIRD_PARTY_NOTICES.md`.

Pin assignments, the AXP2101 register sequence that powers the panel, and the QSPI init command list (except the column and page window, which this firmware sets to 480×480) are adapted from [78/xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) `main/boards/waveshare/esp32-c6-touch-amoled-2.16/` revision `a73e9a7e266e3196d1368ba46c30bee0d81f0a64` (MIT). Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd. Copyright (c) 2025 Project Contributors. Xiaozhi cloud transport, Opus, and audio pipeline code are not included.

`third_party/lvgl_kawaii_face/` is an unmodified copy of [0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face) at commit `d58e1c8f8e7baa7bc93d5ae0dc18affee7f3ccbb` (MIT, Copyright (c) 2026 Eric Nam). It is not compiled into the image.

The ESP-IDF component manager downloads these at build time from `main/idf_component.yml`. They are not vendored:

- `espressif/esp_websocket_client` 1.8.0 (Apache-2.0)
- `espressif/esp_lcd_sh8601` 2.0.1 (Apache-2.0)

ESP-IDF itself is Apache-2.0 and is not vendored. Waveshare's own SDK samples are not copied: those trees do not ship an SPDX header.
