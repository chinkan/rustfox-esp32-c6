# lvgl_kawaii_face (vendored, not linked)

Unmodified sources from [0015/lvgl_kawaii_face](https://github.com/0015/lvgl_kawaii_face) commit `d58e1c8f8e7baa7bc93d5ae0dc18affee7f3ccbb`.

MIT License. Copyright (c) 2026 Eric Nam. See `LICENSE`.

v0 does not compile this widget. It draws on LVGL 9 canvases sized to the parent. A 480×480 RGB565 canvas is about 450 KB, and this ESP32-C6 board has 512 KB of HP SRAM and no PSRAM, which is also the Wi-Fi budget. The firmware that actually runs is `components/rf_face`: a 96×96 fox, client-side emotion, and a blink timer. Emotion names line up with this widget where they overlap (`neutral`, `happy`, `sad`, `surprised`, `angry`, `sleepy`, `worried`). Blink here is a timer overlay, matching `auto_blink` rather than a separate on-wire emotion.

To try the widget later, add `lvgl/lvgl` >= 9 and build it against a partial LVGL parent (on the order of 128×128), not a full-panel canvas. Do not copy Waveshare sample bitmaps; those trees do not carry an SPDX header.
