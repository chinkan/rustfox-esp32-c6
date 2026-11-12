# Third-party notices

This repository is MIT, Copyright (c) 2026 chinkan.ai. See `LICENSE`.

## 78/xiaozhi-esp32

Pin assignments, the AXP2101 register sequence that powers the panel, and the
QSPI init command list (except the column/page window, which v0 sets to
480×480) are adapted from:

`main/boards/waveshare/esp32-c6-touch-amoled-2.16/`
revision `a73e9a7e266e3196d1368ba46c30bee0d81f0a64`
https://github.com/78/xiaozhi-esp32

MIT License.

Copyright (c) 2025 Shenzhen Xinzhi Future Technology Co., Ltd.
Copyright (c) 2025 Project Contributors

Xiaozhi cloud transport, Opus, and audio pipeline code are not included.

## 0015/lvgl_kawaii_face

Vendored unmodified under `third_party/lvgl_kawaii_face/` at commit
`d58e1c8f8e7baa7bc93d5ae0dc18affee7f3ccbb`. Not compiled into the v0 image.

MIT License. Copyright (c) 2026 Eric Nam.

## Espressif components fetched at build time

Not vendored. The IDF component manager downloads them from
`main/idf_component.yml`:

- `espressif/esp_websocket_client` 1.8.0 — Apache-2.0
- `espressif/esp_lcd_sh8601` 2.0.1 — Apache-2.0
- `espressif/esp_codec_dev` 1.6.2 — Apache-2.0 (ES7210 and ES8311)

ESP-IDF itself is Apache-2.0 and is not vendored.

## CST9220 / CST9217 register map

`main/board_touch.c` is original. The command-page id read and the `0xD000`
report follow the public CST92xx map. ESPHome's CST9220 driver and Lewis He's
SensorLib `TouchDrvCST92xx` (both MIT) document that map. Neither tree is
vendored. This firmware does not write the touch-report ack, so a hold is not
cleared by the controller.
