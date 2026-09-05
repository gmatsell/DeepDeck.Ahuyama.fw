# DeepDeck Ahuyama Firmware — Project Context

This is the firmware for the DeepDeck Ahuyama, an open-source 4x4 mechanical macropad based on ESP32.
Built with ESP-IDF (C). Flash via `idf.py build flash`.

## Architecture Overview

- `main/main.c` — boot sequence, initialises all subsystems
- `main/keyboard_config.h` — central config: pin assignments, matrix size, MACRO_LEN, REPORT_LEN, etc.
- `main/keypress_handles.c` — key scanning loop, macro/modifier/layer logic, builds HID report
- `main/deepdeck_tasks.c` — FreeRTOS tasks (key_reports, oled_task, encoder_report, etc.)
- `main/server.c` — HTTP REST API (layers, macros, LED, WiFi)
- `main/wifi_handles.c` — WiFi init: STA mode with AP fallback, starts web server on connect
- `components/ble/hal_ble.c` — BLE HID stack, keyboard/mouse/media queues
- `components/rgb_led/src/rgb_led.c` — WS2812 LED driver, global lighting modes
- `components/rgb_led/include/rgb_led.h` — LED structs (rgb_mode_t, rbg_key), TODO list
- `main/nvs_keymaps.h/.c` — NVS persistence for layers and macros
- `components/ble/hid_dev.c` — HID report dispatch over BLE GATT

## Key Data Structures

```c
// keyboard_config.h
#define MACRO_LEN 5          // keys per macro (chord, not sequence)
#define MACROS_NUM 40
#define USER_MACROS_NUM 200
#define MATRIX_ROWS 4
#define MATRIX_COLS 4
#define ENCODER_SIZE 5       // gestures per knob
#define GESTURE_SIZE 6
#define REPORT_LEN (MOD_LED_BYTES + MACRO_LEN + MATRIX_ROWS * KEYMAP_COLS)  // = 23 bytes

// dd_layer — one layer of key mappings (stored in NVS)
// dd_macros — one macro: name, keycode trigger, key[MACRO_LEN] chord

// rgb_mode_t — global LED mode: { mode, H, S, V, speed, rgb[3] }
// rbg_key    — per-key LED state: { h, s, v }  (only used for press flash currently)
```

## Planned / Requested Enhancements

### 1. Sequential Macros (longer key sequences)
**Current behaviour:** macros are chords — all keys in `key[MACRO_LEN]` are pressed simultaneously
in a single HID report (positions `[2..2+MACRO_LEN-1]`).

**Goal:** support sequences of keystrokes (e.g. typing a password), not just chords. This also
allows longer macros without touching the BLE/HID layer.

**Approach:**
- Keep chord support for modifier combos (Shift+C, Ctrl+Alt+Del)
- Use `check_modifier()` (already in `keypress_handles.c`) to hold modifier keys across
  subsequent non-modifier presses, releasing all at the end
- For multi-step sequences, each step is a small chord; steps are separated by press+release cycles
- DO NOT increase `MACRO_LEN` and `REPORT_LEN` — this would require changing the BLE HID
  descriptor and re-pairing all devices. Work within the existing report size instead.
- Consider a flag/sentinel value in the key array (e.g. `KC_NO` already acts as terminator)
  to distinguish "hold modifier" vs "new chord step"

**Files to change:** `main/keypress_handles.c`, `main/keyboard_config.h` (possibly new struct),
`main/nvs_keymaps.c` (if storage format changes), `main/server.c` (API for new format)

---

### 2. Per-Key RGB Colors per Layer
**Current behaviour:** 5 global lighting modes applied to all 16 LEDs at once (off, pulsating,
color cycle, rainbow, solid). Mode 5 lights keys that have a non-zero keycode, but still one
global colour. The WS2812 hardware supports individual addressing — it's just not used for
per-key colour.

**Goal:** each key on each layer has its own stored RGB colour, displayed when that layer is active.

**Approach:**
1. Add `uint8_t key_rgb[MATRIX_ROWS][MATRIX_COLS][3]` to the `dd_layer` struct in `nvs_keymaps.h`
2. Update `nvs_write_layer` / `nvs_load_layouts` in `nvs_keymaps.c` to persist the new field
3. Add a new LED mode (e.g. mode 6 = "per-key") in `rgb_led.c` → `key_led_modes()` that
   iterates the matrix and calls `rgb_key->set_pixel(rgb_key, index, r, g, b)` per key
4. Trigger a LED refresh in `key_led_modes()` whenever `current_layout` changes
5. Extend the layer JSON in `server.c` — `get_layer_url_handler` and `update_layer_url_handler`
   — to include colour per key so the web portal can read/write it

**Hardware note:** `rgb_key->set_pixel()` and `rgb_key->refresh()` already work correctly for
individual addressing. LED index = `row * MATRIX_COLS + col` (same as `(row << 2) + col` used
in `rgb_key_led_press()`).

**Files to change:** `main/nvs_keymaps.h`, `main/nvs_keymaps.c`, `components/rgb_led/src/rgb_led.c`,
`components/rgb_led/include/rgb_led.h`, `main/server.c`

---

## Known Issues / Incomplete Code
- `delete_macro_url_handler` in `server.c` returns 200 without actually deleting anything
- `config_url_handler` returns hardcoded strings ("ssid", "MAC") instead of real values
- `json_response()` helper has "TO DO" placeholder fields throughout
- WiFi and BLE cannot run simultaneously on ESP32 — firmware handles this but it's a known constraint
- NVS storage is the bottleneck for layer count (~15 max due to memory)

## Build Notes
- Requires ESP-IDF toolchain
- `WIFI_ENABLE`, `OLED_ENABLE`, `RGB_LEDS`, `GESTURE_ENABLE` are compile-time defines in `keyboard_config.h`
- Web portal frontend is served from SPIFFS (`spiffs_image/` directory), gzip-compressed
- mDNS hostname is "Ahuyama" (disabled by default, enable with `#define USE_MDNS`)
