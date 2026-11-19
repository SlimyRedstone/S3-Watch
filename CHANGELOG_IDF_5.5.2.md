# Migration log: ESP-IDF v5.4.3 -> v5.5.2

Date: 2026-11-13. Target: ESP32-S3, Waveshare ESP32-S3-Touch-AMOLED-2.06.
Toolchain used by the 5.5.2 build: xtensa-esp-elf GCC 14.2.0 (`esp-14.2.0_20251107`), same major as 5.4.3.

## 1. Verdict

- The build break was **not** caused by ESP-IDF 5.5.2. All errors were in `components/Settings/Settings.cpp`
  and would fail on any IDF version: the C bridge for the EEZ-generated UI referenced `cfg` and `Settings`
  outside their namespace, and used struct fields (`user_wake_up`, `sleep_charge`, `vibrations_enabled`,
  `wifi_enabled`, `bluetooth_enabled`, `upgrade_enabled`, `espnow_enabled`) that do not exist in
  `Settings::AppConfig`. The committed HEAD (`fb2ec52`) contains the same code, so HEAD did not build on 5.4.3 either.
- No ESP-IDF 5.5 API change touches this codebase. Checked against the 5.5.2 headers and the official 5.5
  migration guide (touch sensor driver, TWAI driver, `esp_flash_os_functions_t::start` flags,
  `esp32s3/rtc.h` -> `esp_rtc_time.h`, Log V2, HTTP server header option, ESP-NOW send callback signature).
  None of these are used.
- Evidence the rest of the tree is 5.5.2-clean:
  - The failed build had already compiled 1735/2052 targets, including `Podometer.cpp`, the ULP RISC-V
    firmware (`ulp_pod`), XPowersLib, brookesia_core, every managed component, and the Settings UI `.c` files.
  - After the fixes, all 10 project translation units (`Settings.cpp`, `bsp_board_extra.c`, `BLEMouse.cpp`,
    `BLEMultiMedia.cpp`, `Flashlight.cpp`, `Recorder.cpp`, `RemoteNow.cpp`, `main.cpp`, `ClockApp.cpp`,
    `Podometer.cpp`) pass a `-fsyntax-only` pass using the exact flags from `build/compile_commands.json`
    and the 5.5.2 compiler. No build was run and no object files were written. Link and runtime are untested.

## 2. Files changed

### components/Settings/Settings.cpp
| Change | Reason |
|---|---|
| Added `using esp_brookesia::apps::Settings;` before the `extern "C"` block | `Settings::cfg` / `Settings::sleep_locked` were not visible from global scope (errors at lines 55-116). |
| Getters/setters now read and write `Settings::cfg` directly. Field mapping: `user_wake_up` -> `enable_user_button_wake_up`, `sleep_charge` -> `disable_sleep_on_charge`, `vibrations_enabled` -> `enable_vibrations`, `wifi_enabled` -> `enable_wifi`, `bluetooth_enabled` -> `enable_bluetooth`, `espnow_enabled` -> `enable_espnow`, `upgrade_enabled` -> `enable_upgrade` (new field) | The old names did not exist. The duplicate C globals (`bool dim_screen, user_wake_up, ...`) were removed: they were never initialised from the persisted config, so every switch rendered "off" regardless of the SD file. Behaviour change: switches now show the persisted value. |
| `get_version_value()`: buffer made `static` | Returned the address of a stack array (`-Wreturn-local-addr`). The generated `screens.c` stores that pointer as label text: use-after-return. |
| `set_version_value()`: `sizeof(value)` -> `sizeof(version_text) - 1` + NUL terminator | `sizeof(value)` is the pointer size (4), so at most 3 characters were copied. |
| `action_slider_changed()`: `(void)v;` | Unused-variable warning. Logic untouched (still a stub). |
| `loadFromSd()`: removed the six `cJSON_Delete(aa..af)` calls; added reading of `dim_screen` | `cJSON_GetObjectItem()` returns items owned by the root object, and `cJSON_Delete()` also frees every `->next` sibling. The old code therefore freed most of the `settings` object, then `cJSON_Delete(j)` freed it again: heap corruption on every boot where `/sdcard/.settings.json` exists. `dim_screen` was written by `saveToSd()` but never read back. |

### NimBLE host log level
- `CONFIG_BT_NIMBLE_LOG_LEVEL_WARNING=y` (was INFO). The host logged `GATT procedure initiated: notify` +
  `att_handle=..` for every HID report sent (mouse move at 30 ms, each media key twice). Compiled out now.

### BLE Mouse gestures
- `components/BLEMouse`: two-finger vertical drag scrolls (wheel notch every 10 px of travel, slide down = scroll
  down; scroll mode is latched for the whole press so a finger lifting early does not move the cursor). Quick single-finger
  tap (< 250 ms, < 12 px) sends a left click. Edge-swipe "back" is unchanged and suppressed for taps and scrolls.
  Second finger is detected via the FT5x06 touch-count register (register 0x02), as before; the 5.5.2 I2C driver accepts the
  second device handle at the touch address.
- Rev 2: scrolling now measures the mean Y of both fingers read straight from the controller (`read_touch()`, one 13-byte
  register read: TD_STATUS + two touch records) instead of LVGL's single point. The controller is free to swap which finger
  it reports first; with LVGL's point that swap either cancelled the travel or tripped the jump filter, so no notch was ever
  sent. One finger lifting early pauses scrolling instead of moving the cursor. `ESP_UTILS_LOGD` traces added
  (`scroll: start`, `scroll: dy=.. wheel=..`); raise the BLEMouse log level to debug to see them.
- Rev 3: the FT3168 datasheet (files.waveshare.com/wiki/common/FT3168.pdf, v0.6) says "single point touch and gesture
  **or** two-point touch" (self-capacitive); the mode is a firmware option and the board most likely ships in 1-point mode,
  so a finger count of 2 may never be reported. Added: (a) `touch: fingers=N` INFO trace on every count change, which
  settles that on the console; (b) a right-edge scroll strip (`SCROLL_STRIP_PX`=48, faint shaded column with up/down
  arrows): a one-finger drag that starts there scrolls (same notch rule) and does not move the cursor or count as a tap.
  Two-finger scroll is kept for firmware that does report two points.

### Bluetooth status-bar icon
- `components/Settings/icon/bluetooth_icon_2.c` (user-supplied, 16x16 ARGB8888) shown in status-bar area 2 next to the SD
  icon while a BLE central is connected. `Settings::setBluetoothConnected()` adds/removes icon id 1002; the Settings status
  timer re-syncs in case the event arrived before the status bar existed. `BLEMouse::onConnectionChanged()` calls it under
  the LVGL lock. BLEMouse now `REQUIRES Settings` (no cycle: Settings does not depend on BLEMouse). The 56x56
  `bluetooth_icon.c` is compiled but unused.

### Display: internal DMA draw buffers (fixes `panel_io_spi_tx_color(395): spi transmit (queue) color failed`)
- `main/main.cpp`: after `bsp_display_start_with_config()`, LVGL is switched to two 410x20 px internal DMA buffers
  (`lv_display_set_buffers`, partial mode, double buffered). The BSP's single 410x50 px buffer is `malloc`ed and lands in
  PSRAM; the SPI master does not consider PSRAM DMA-capable and allocated a fresh internal bounce copy (up to 41 kB) for
  every stripe, freed in the DMA-done ISR. With BLE loaded the internal heap fragments, that allocation fails with
  `ESP_ERR_NO_MEM`, and `esp_lcd` logs the error once per stripe with a torn frame. Falls back to the BSP buffer if the two
  allocations fail at boot (logged).
- `components/BLEMouse/BLEMouse.cpp`: `onConnectionChanged()` runs on the NimBLE host task (from `gap_event`) and touched
  LVGL without the lock. Now wrapped in `bsp_display_lock(1000)`/`bsp_display_unlock()`.

### Core split (radio on core 0, UI on core 1)
- `sdkconfig` / `sdkconfig.defaults`: `CONFIG_BT_NIMBLE_PINNED_TO_CORE_0=y` (host now with the controller on core 0, was core 1
  next to LVGL), `CONFIG_LWIP_TCPIP_TASK_AFFINITY_CPU0=y` (was unpinned).
- `main/main.cpp`: new `run_on_core()` helper. `bsp_i2c_init()` and `bsp_display_start_with_config()` are executed on a
  task pinned to core 1 so the I2C and LCD SPI interrupts are allocated on the UI core (drivers use the calling core
  with the default `isr_cpu_id`). `mem_info` pthread pinned to core 1.
- `ClockApp` `ClockSleep` task pinned to core 1; Podometer `imu_pod`/`pod_bg` and Recorder `rec` pinned to core 0.
- Not changed by this: shared heap, flash-cache stalls on NVS writes, the single SPI panel handle and I2C bus, DFS.

### main/main.cpp
- `mem_info` thread: `boost::this_thread::sleep_for(5 s)` replaced by `vTaskDelay(pdMS_TO_TICKS(5000))`. On 5.5.2 the task
  watchdog fired every 5-15 s with `IDLE0` starved and `mem_info` as the running task on CPU 0 in three consecutive samples,
  each inside boost's `wait_until`/`pthread_cond_timedwait` polling loop or the `sprintf` right after it. That thread also
  takes the LVGL lock on every pass, which is what made touch sluggish. IDF's `pthread_cond_timedwait`, newlib `time.c`
  and esp-boost 0.3.1 are byte-identical to the 5.4.3 setup, so the exact trigger is not identified; the fix removes the
  only boost sleep in this firmware (the other users are in the disabled speaker/anim_player code).

### components/Settings/settings_ui_bridge.h (new) + components/Settings/CMakeLists.txt
- Second build attempt failed in the generated `ui/screens.c`: it calls `get_sleep_charge_button_value()` but the
  generated `ui/actions.h` declares neither that getter nor its setter. GCC 14 rejects implicit function declarations
  in C (`-Werror=implicit-function-declaration` is the default). The `ui/` folder is generated, so instead of editing it
  the two prototypes live in `settings_ui_bridge.h`, which CMake force-includes into `ui/screens.c`
  (`COMPILE_FLAGS "-include ..."`) and which `Settings.cpp` also includes. Delete both once the EEZ export declares them.

### components/Settings/Settings.hpp
- Added `bool enable_upgrade = false;` to `AppConfig` (backs the existing `get_upgrade_value()` / `set_upgrade_value()` bridge). Not persisted to SD on purpose; nothing consumes it yet.

### components/bsp_extra/src/bsp_board_extra.c
- Removed `#include "driver/i2c.h"`. Nothing in the file uses the legacy I2C driver (the BSP uses `i2c_master`).
  In 6.0 this header prints an end-of-life `#pragma message` and it is scheduled for removal in 7.0.

### CMakeLists.txt
- `set(CMAKE_NINJA_FORCE_RESPONSE_FILE ON)` before `include(project.cmake)`. Third build attempt died at the very end with
  `ninja: fatal: CreateProcess: The parameter is incorrect. (is the command line too long?)` while compiling
  `build/project_elf_src_esp32s3.c`. That stub is compiled with the public include directories of every component in the
  link (316 `-I` entries, 32 661 characters under 5.5.2, versus the Windows limit of 32 767). 5.4.3 stayed just below the
  limit; 5.5.2 adds `esp_driver_twai`, `esp_driver_touch_sens`, new `esp_hw_support/*` and NimBLE service dirs. With the
  flag, CMake's Ninja generator passes compiler arguments via `@file.rsp` for every target. No effect on the ULP sub-build
  (separate CMake invocation) and `compile_commands.json` still lists the expanded commands.
- `cmake_minimum_required(VERSION 3.5)` -> `3.16`. ESP-IDF 5.5 itself requires CMake 3.22; CMake 3.27+ warns on
  policy versions below 3.10 and CMake 4.x refuses anything below 3.5. No functional change.

### README.md
- Requirement line now says v5.5.2.

### sdkconfig (already modified in the working tree before this session)
Regenerated by 5.5.2. Diff reviewed; nothing project-relevant was dropped:
- SOC capability renames (`SOC_MEMSPI_SRC_FREQ_120M` -> `..._SUPPORTED`, `SOC_SPI_MEM_SUPPORT_OPI_MODE` -> `..._FLASH_OPI_MODE`, new touch/TWAI/UART caps).
- `CONFIG_NEWLIB_*` options now mirrored as `CONFIG_LIBC_*` (`CONFIG_LIBC_NEWLIB=y`; both spellings kept).
- Log V1 and bootloader log V1 made explicit (`CONFIG_LOG_VERSION=1`), i.e. unchanged behaviour.
- New IRAM-placement options default-on (`CONFIG_FREERTOS_IN_IRAM`, `CONFIG_ESP_TIMER_IN_IRAM`, `CONFIG_LOG_IN_IRAM`, `CONFIG_PM_SLP_IRAM_OPT`, ...).
- NimBLE options reordered only; `S3Watch` device name, bonds, NVS persist unchanged.
- `CONFIG_ESP_TLS_USE_DS_PERIPHERAL=y` dropped (no TLS used).
- Still present and required: `CONFIG_ULP_COPROC_ENABLED/TYPE_RISCV/RESERVE_MEM=6144`, `CONFIG_BT_NIMBLE_ENABLED`, `CONFIG_PM_ENABLE`, SPIRAM octal 80 MHz, custom partition table.
- Note: `sdkconfig.defaults` does not contain the ULP, Bluetooth or PM options. The committed `sdkconfig` is the only place they live. Do not delete it.

### sdkconfig: boot crash `BLE_INIT: Malloc failed` / `BLE assert emi.c 164` (internal RAM exhausted)
Cause: three IRAM options turned on in the working tree (`CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM`,
`CONFIG_LWIP_IRAM_OPTIMIZATION`, `CONFIG_LWIP_EXTRA_IRAM_OPTIMIZATION`) grew `.iram0.text` from 0x1C113 to 0x2C1F7 B.
On the S3, IRAM and DRAM are the same SRAM, so the internal heap at app start shrank by 65 KB (heap start moved
0x3FCA4600 -> 0x3FCB4700; about 155 KB left). The BLE controller allocates with `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA`
only, got NULL during `esp_bt_controller_enable()`, and the controller library asserts instead of checking (`emi.c 164`).
Fix:
- The three IRAM options set back to off (same as the committed sdkconfig).
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` 4096 -> 65536 (sdkconfig and sdkconfig.defaults). Plain `malloc()`/`new`
  (LVGL objects, Brookesia, std::string) now spills to PSRAM once internal free drops under 64 KB, so controller,
  Wi-Fi, SPI and the DMA draw buffers always find internal RAM. `heap_caps_malloc(MALLOC_CAP_INTERNAL)` callers are
  not affected by the reserve.
Not changed, still costing internal RAM in the working tree: `CONFIG_HEAP_POISONING_LIGHT` (8 B per allocation),
NimBLE host stack 8192, `CONFIG_ESP_WIFI_IRAM_OPT`/`RX_IRAM_OPT` (about 25 KB IRAM, off is fine for ESP-NOW).

### dependencies.lock / managed_components
Re-resolved by the component manager for `idf 5.5.2`. Every component version is identical to the 5.4.3 lock
(lvgl 9.2.2, esp_lvgl_port 2.4.4, waveshare BSP 1.0.6, esp_codec_dev 1.3.6, esp-lib-utils 0.3.0, esp-boost 0.3.1, ...).
Both files are git-ignored.

## 3. APIs used by the project, checked unchanged in 5.5.2

| Area | Symbols | Status |
|---|---|---|
| I2C master | `i2c_master_bus_add_device`, `i2c_master_transmit(_receive)`, `i2c_master_bus_rm_device` | unchanged |
| GPIO / RTC IO | `gpio_config`, `gpio_wakeup_enable`, `gpio_deep_sleep_hold_en/dis`, `gpio_reset_pin`, `rtc_gpio_*` | unchanged |
| Sleep | `esp_sleep_enable_ext1_wakeup`, `esp_sleep_enable_ulp_wakeup`, `esp_sleep_enable_gpio_wakeup`, `esp_sleep_pd_config`, `esp_light_sleep_start`, `esp_deep_sleep_start`, `esp_sleep_get_wakeup_cause` | unchanged (ext1 variant carries a "will be deprecated in 6.0" doc note, see TODO file) |
| ULP RISC-V | `ulp_riscv_load_binary`, `ulp_riscv_run`, `ulp_riscv_halt`, `ulp_riscv_timer_stop`, `ulp_set_wakeup_period`, `ulp_embed_binary()` | unchanged, firmware built |
| Power mgmt | `esp_pm_configure`, `esp_pm_config_t{max,min,light_sleep_enable}` | unchanged |
| NimBLE | `nimble_port_init`, `ble_gatts_notify_custom`, `ble_svc_gap_device_name_set`, `ble_store_util_delete_peer`, `ble_store_config_init`, `ble_sm_inject_io` | unchanged |
| Storage | `nvs_*`, `esp_vfs_fat.h`, `esp_vfs_common.h`, `esp_vfs.h` (still pulls `<sys/stat.h>`, `<unistd.h>`, `<dirent.h>` that Recorder/Podometer rely on transitively) | unchanged |
| Misc | `esp_timer_get_time`, `esp_rom_delay_us`, `heap_caps_*`, `RTC_DATA_ATTR`, `esp_random` | unchanged |

## 4. Known broken or fragile, NOT fixed (pre-existing, unrelated to the IDF bump)

1. **Settings UI does nothing persistent.** The generated switches call `action_button_pressed()` (empty) and never
   call the `set_*_value()` setters; `saveToSd()` is only reachable from the old, commented-out +/- buttons.
   Toggles are lost at reboot. Needs wiring in `Settings.cpp` (the `ui/` folder must stay untouched).
2. **Generated-UI warnings** in `components/Settings/ui/screens.h` (`create_text/create_slider/create_switch` declared
   `static` but never defined). Comes from the EEZ export; left alone. In 6.0 these become errors (see TODO file).
3. **LVGL private struct access**: `t->user_data` (`main/main.cpp` clock timer lambda) and `touch->state`
   (`ClockApp.cpp`) poke `lv_timer_t` / `lv_indev_t` internals. Fine with the pinned lvgl 9.2.2, breaks on 9.3.
   Use `lv_timer_get_user_data()` / `lv_indev_get_state()` when convenient.
4. `Recorder.cpp`: `update_sd_label()` is a stub and the spectrum timer is commented out. `s_bar_db` is written by
   the recorder task and never displayed.
5. `RemoteNow.cpp`: ESP-NOW is not implemented. When it is, note the 5.5 breaking change: the send callback is
   `void cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)` (was `const uint8_t *mac_addr`).
6. `Settings::init()` calls `s_pmu.isVbusIn()` even if `pmu_open()` failed. Safe (the read callback returns -1) but
   the resulting `sleep_locked` value is meaningless in that case.
7. `.vscode/settings.json` (`idf.espIdfPathWin`) and `.vscode/c_cpp_properties.json` still point IntelliSense at
   the v5.4.3 tree. Git-ignored, machine-local; `idf.currentSetup` already points at 5.5.2. Not changed.
8. The installed framework reports `v5.5.2-dirty`: the IDF tree at
   `C:/Users/SlimyRedstone/ESP-IDF/.espressif/v5.5.2/esp-idf` has local modifications. Unknown what or why.
9. Untestable here: light/deep-sleep paths, ULP bit-banged I2C, PMU rail handling, NimBLE pairing. 5.5 touched
   sleep internals (new `CONFIG_PM_SLP_IRAM_OPT`, `CONFIG_ESP_SLEEP_*` workarounds). Watch the first sleep/wake
   cycle and the ULP batch wake on hardware.

## Settings app: every Display / Audio / Bluetooth control now does something

`components/Settings` (UI in `ui/` untouched; the generated code gives one shared callback per widget type and no user
data, so widgets are identified by their row title, e.g. "Time before light sleep").

- `Settings::AppConfig` restructured to mirror the JSON: `cfg.display.*`, `cfg.audio.*`, `cfg.bluetooth.enabled`,
  `cfg.wifi.enabled`, `cfg.espnow.enabled`, `cfg.upgrade.enabled`. `inactivity_delay_ms` is gone. File layout:
  `settings.display{brightness, dim_screen, user_button_wake_up, disable_sleep_on_charge, light_sleep_brightness,
  light_sleep_delay_s, deep_sleep_delay_s}`, `settings.audio{vibrations, vibration_duration, volume}`,
  `settings.bluetooth{enabled}`, plus `wifi`/`espnow`/`upgrade{enabled}`. Old flat files are ignored (defaults apply,
  file rewritten on first save). Defaults = the generated sliders' initial values (85, 80, 15 s, 30 s, 30).
- Slider values are overwritten from `cfg` right after `Settings_ui_init()` (`syncSlidersFromCfg()`); switches already
  read the getters. Switch changes were never reaching the setters (generated code only hooks `action_button_pressed`),
  now mapped by row title. Saves are debounced (1 s after the last change) so a slider drag is one SD write.
- Brightness: live `bsp_display_brightness_set()` under the LVGL lock (`Settings::setPanelBrightness()`).
  `ClockApp::setBacklight(true)` now restores `cfg.display.brightness` instead of the BSP's fixed 100 %.
- Sleep ladder (`ClockApp::sleepMonitorTask`): idle >= `light_sleep_delay_s` -> `enterLightSleep()` (was: straight to
  deep sleep). Light sleep dims the panel to `light_sleep_brightness` (1..255 slider -> %) when "Dim screen on light
  sleep" is on, else panel off; wake on touch INT, plus GPIO0 only when "User button wake-up" is on; timer wake at
  `deep_sleep_delay_s` -> `enterDeepSleep()`. "Disable sleep when charging" skips both while VBUS is present
  (`Settings::isCharging()`, AXP2101). While the BLE stack is up, `esp_light_sleep_start()` is replaced by a polling
  wait (`waitForInput()`): the controller does not survive forced light sleep with modem sleep off.
- Deep sleep + "User button wake-up": ESP deep sleep with ext1 on GPIO0 (rails stay up) instead of the AXP2101 shutdown,
  which only the power key can wake. Off: unchanged PMU shutdown. The Podometer's ULP handler (when it owns deep sleep)
  always uses ext1 on GPIO0.
- Vibrations: motor on GPIO18 (schematic "Motor" block; same pin as Waveshare's factory VibrationTest), active high,
  `Settings::vibrate(ms)` with an esp_timer off-switch, capped at 1 s. A press hook on the pointer indev
  (`lv_indev_add_event_cb(.., LV_EVENT_PRESSED)`) ticks on buttons, switches, sliders, checkboxes, dropdowns, rollers,
  text areas, button matrices, arcs, spinboxes, keyboards, menu rows and clickable images/labels (launcher icons).
  Plain containers do not vibrate (LVGL makes them clickable by default). `vibration_duration` defaults to 100 ms.
- Volume: `Settings::setVolume()` -> `bsp_extra_codec_volume_set()` when the codec is up (`bsp_extra_codec_is_init()`,
  new in `components/bsp_extra`). Recorder's lazy codec init now starts at `cfg.audio.volume` (was 60) and its own
  slider writes through Settings. Recorder `REQUIRES Settings`.
- Bluetooth: `BLEMouse::init()` no longer starts NimBLE unconditionally. It calls `Settings::ensureLoaded()` (app init
  order is undefined), registers `ble_set_enabled()` via `Settings::setBluetoothHandler()`, and starts the stack only if
  `cfg.bluetooth.enabled`. The switch starts/stops at runtime: stop = `nimble_port_stop()` + `nimble_port_deinit()`
  (controller memory kept, so it restarts), with GAP callbacks muted during the stop so the host task cannot block on
  the LVGL lock held by the switch handler. `Settings::bluetooth_running` tells ClockApp which light-sleep path to use.
- Removed: the old `sleep_locked = isVbusIn()` at Settings init and in the wake-up switch setter (it blocked sleep on
  USB regardless of any setting); the dead +/- delay UI code.
- Settings `REQUIRES` gains `esp_timer bsp_extra`.

### Runtime crash `assert failed: bt_osi_mem_malloc bt_osi_mem.c:40 (mem != NULL)` when switching Bluetooth on
Internal SRAM was exhausted at runtime: LVGL used plain `malloc()`, and with `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=8192`
every small allocation (objects, styles, draw tasks: the whole launcher and every app screen) landed in internal SRAM
first. Boot had room for the controller; 18 s of UI later the NimBLE host could not get a sub-kilobyte block.
- `main/lv_mem_core_psram.c` (new) + `CONFIG_LV_USE_CUSTOM_MALLOC=y`: LVGL allocator with a floor policy. Blocks of
  4 KB and more (layer buffers, snapshots) go to PSRAM. Smaller ones stay in internal SRAM as long as internal free stays
  above 112 KB after the allocation, else PSRAM. Rev 1 put everything in PSRAM: draw tasks are allocated per frame and
  the object/style tree is read per redraw, so the frame rate halved and the partial-stripe flush showed as tearing.
  With the floor the UI runs from SRAM as before, while the radio always finds its memory; once BLE is up (it lives
  inside the floor) LVGL keeps spilling to PSRAM until it is stopped. The DMA draw buffers are still the two internal
  ones from `main.cpp`.
- `main/CMakeLists.txt`: `WHOLE_ARCHIVE` on the main component. liblvgl references `lv_malloc_core()` & co., main's
  archive is scanned first and nothing in it uses them, so the linker dropped the object (undefined `lv_mem_init`).
- `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y`: NimBLE host heap (mbufs, GATT/ATT state) in PSRAM. The controller
  cannot (internal only) and stays there. Both in `sdkconfig` and `sdkconfig.defaults`.
- `BLEMouse.cpp ble_set_enabled()`: refuses to start when internal SRAM is below 72 KB free / 24 KB largest block
  (the controller library asserts instead of failing). The Settings handler now returns `bool`; a refused start logs the
  numbers, keeps `cfg.bluetooth.enabled = false` and un-checks the switch.
- `main.cpp` mem_info thread: one INFO line every 5 s with SRAM/PSRAM free and largest block
  (`heap: SRAM .. free / .. max block | PSRAM ..`). `EXAMPLE_SHOW_MEM_INFO = false` silences it.

## 5. What to do now

1. `idf.py build` (full build, link untested by me).
2. Flash, boot with an SD card that already has `.settings.json` (this used to corrupt the heap).
3. Exercise: Clock -> dim -> deep sleep -> button wake; Podometer ULP wake; BLE Mouse pairing; Recorder record/playback.
