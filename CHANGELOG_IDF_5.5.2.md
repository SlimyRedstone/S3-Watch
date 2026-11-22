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

## Settings: Developer > "Enable Performance counter"
- `settings.developer.performance_counter` (bool, default false) loaded/saved with the rest. The switch is mapped by its row
  title like the others. `Settings::applyPerformanceCounter()` hides/shows LVGL's perf monitor label (created by LVGL at
  display creation, `CONFIG_LV_USE_PERF_MONITOR`) and pauses/resumes its refresh timer; applied at `Settings::init()` and
  on every switch change. `lv_sysmon_show_performance()` is not called again: in 9.2.2 it creates a second label and timer.
- `settings_ui_bridge.h`: prototypes of `get/set_developer_performance_value()` (the regenerated `screens.c` calls the
  getter, `actions.h` does not declare it: implicit declaration error in C with GCC 14).

## Sonometer: FFT tab blocking the touch screen
- `fft_render()` is incremental: only the rows between a column's old and new bar top are written (was a full rewrite of
  the 96 KB A8 plane in PSRAM per spectrum, in the LVGL task). Returns the changed rectangle; `fft_timer_cb()` invalidates
  that instead of the whole plot, so quiet parts of the spectrum are not redrawn or re-sent over QSPI.
- `FFT_UPDATE_PERIOD_MS` 20 -> 50: 20 spectra/s instead of 50. 50 full-plot redraws per second exceeded what the LVGL core
  renders, so it never got idle time to process touch. Also 2.5x less PSRAM churn from `fft_task` on core 0: the ESP32-S3
  data cache (64 KB) is shared by both cores, so the FFT buffers in PSRAM evicted the fonts/images core 1 renders from.

## Recorder / BLEMultiMedia: double-prefixed EEZ symbols (`Recorder_RemoteNow_Recorder_objects`)
- `components/{Recorder,BLEMultiMedia,Sonometer}/update_eez.py` were copies of RemoteNow's with `APP_NAME = "RemoteNow"`.
  Recorder and BLEMultiMedia run theirs at CMake configure: it prefixed their already-prefixed ui/ symbols with
  `RemoteNow_`, then `dev/prefix_eez.py` added the component prefix again. Recorder.cpp / BLEMultiMedia.cpp no longer
  compile. `APP_NAME` fixed in the three scripts (Sonometer's is not run by its CMakeLists, fixed anyway).
- The ui/ files were not touched by hand. Their diff against HEAD is only that rename, so restoring them is safe:
  `git checkout -- components/Recorder/ui components/BLEMultiMedia/ui`.

### Performance counter never visible (fix)
- Brookesia's `Display::begin()` replaces `disp->sys_layer` with its own system screen. LVGL had created the perf monitor
  label on the original system layer at display creation, which is never drawn after that, so hiding/showing it did
  nothing. `Settings::applyPerformanceCounter()` now re-parents the label onto the current system layer (once), marks it
  floating, re-aligns it to `LV_USE_PERF_MONITOR_POS` and brings it to the foreground when shown.

### `Bluetooth not started: internal RAM 54727 free / 40960 max block` at boot (fix)
- `CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM` was on again: `.iram0.text` 0x2C183 (180 KB) instead of 0x1C113 (115 KB), and
  internal heap start 0x3FCC3DF0 instead of 0x3FCB3738, about 67 KB less internal SRAM. Turned off in `sdkconfig`, and
  written as explicitly off with the reason in `sdkconfig.defaults`. BLEMouse's start guard (72 KB free / 24 KB block)
  is unchanged: it is what stopped this from becoming the earlier controller assert.

### Internal SRAM moved to PSRAM so Bluetooth can start at runtime
Link map before the change: `.dram0.bss` 41.9 KB, of which 24.2 KB is `Recorder.cpp` (FFT window 4 KB, FFT work buffer
8 KB, file-name table 12 KB) and 5.9 KB the NimBLE host (`libbt.a`). Measured on the watch: 69 KB internal free at 96 s
against the 72 KB the BLE start check asks for.
- `CONFIG_SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`: `EXT_RAM_BSS_ATTR` globals go to PSRAM, and IDF moves the `bt`
  library's `.bss` there by itself (`bt_extram_bss` linker scheme).
- `Recorder.cpp`: `s_fft_wind`, `s_fft_buf` and `refresh_file_list()`'s `names` tagged `EXT_RAM_BSS_ATTR` (24 KB).
- `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` 8192 -> 1024: plain `malloc`/`new` above 1 KB (Brookesia, C++ containers, audio
  player, cJSON documents) goes to PSRAM first. Code that needs internal or DMA memory asks for it explicitly and is
  unaffected; LVGL has its own allocator.
- `main.cpp` mem_info thread stack (4 KB) in PSRAM (`stack_in_ext`): it never touches flash or sleeps.
- `BLEMouse.cpp`: logs the real cost of starting the stack once the host has synced
  (`BLE up: internal RAM A -> B free, stack took N B`). The 72 KB / 24 KB start check is an estimate; set it to the
  measured cost plus a margin.

## Status bar, battery percentage, Upgrade sub-menu, firmware update check

### Status bar (top-right)
- Only 3 icons showed: the icon area was 30 % of 410 px minus the 60 px corner offset (about 63 px), and Brookesia hides
  an icon that does not fit its area when it is added (`StatusBarIcon::updateByNewData`). `main/dark/status_bar_data.hpp`:
  icon area 62 % (room for 5 icons + the battery percentage). It overlaps the clock area; both are transparent.
- `brookesia_core` StatusBar: new public `getAreaObject(index)` (read access to the area containers).
- `Settings::layoutStatusBar()`, run on `LV_EVENT_LAYOUT_CHANGED` of the icon and clock areas: once more than
  `STATUS_ICONS_BEFORE_SHIFT` (3) icons are visible the clock area moves left (`translate_x`) by the width of the extra
  icons, and in any case far enough to keep 8 px from the icon group (covers the percentage text); never further left
  than 40 px from the edge. Written only when the value changes, so the layout pass it triggers settles.
- Wi-Fi icon (`Settings::refreshWifiIcon()`): hidden unless Wi-Fi or ESP-NOW is enabled; then "connected" (3 bars) when
  the station has an IP, else the "Wi-Fi off" glyph. Updated by both switches and every status tick.
  `BLEMouse::refreshStatusIcon()` no longer drives the Wi-Fi icon with the BLE link (the Bluetooth icon does that).
- Battery > "Display percentage": `settings.battery.show_percentage`, getter/setter `get/set_battery_perc_text_value()`
  (prototypes in `settings_ui_bridge.h`, not in `actions.h`). Stylesheet `enable_battery_label = 1`; the label is shown
  or hidden with `showBatteryPercent()` / `hideBatteryPercent()`, text refreshed by the status timer.

### Settings UI (`components/Settings/ui/screens.c|h`, hand-written: the Settings EEZ project has no screens)
- `create_button(parent, text, event_cb, user_id)`: a menu row (`lv_menu_cont`) holding a button with the previous
  styling (content size, label centred, Montserrat 24); `user_id` is the button's user data (a `buttons_t`). The Upgrade
  and About buttons use it; they used to be created straight in the menu section, outside any row.

### Firmware update check (`components/Settings/SettingsUpgrade.cpp`, new)
- `settings.upgrade.src_url`, default `https://api.slimyredstone.net/code/s3-watch/version`; written to the file at boot
  when missing. `loadFromSd()` reads into a 4 KB heap buffer (the file passed 1 KB; it was a 1 KB stack array on the
  main task).
- `Settings::getVersion()` / `getVersionText()`: hard-coded `FW_VERSION` (1.0.0) in `Settings.hpp`. The About page shows
  it instead of the build time.
- `Settings::fetchLatestVersion()`: HTTPS GET (`esp_http_client`, CA bundle, redirects followed), parses
  `{"last":{"t":"v1.2.0","n":[1,2,0]},"url":"..."}` (`n` first, `t` as fallback).
- "Check for updates" -> `Settings::checkForUpdates()`: no Wi-Fi connection -> message asking to enable Wi-Fi and connect
  to a router. Then, on a pthread with its stack in PSRAM: up to date -> message; newer and no SD card -> message that
  the SD card is mandatory, nothing downloaded; newer -> download to `/sdcard/.temp/<file name from the URL>` through a
  `.part` file, progress in the message box, size checked against Content-Length. Installing is not implemented.
- `Settings::isWifiConnected()` checks `esp_netif_get_nr_of_ifs()` first: nothing in the project starts Wi-Fi yet, and
  the handle lookup goes through the lwIP thread, which would hang if lwIP was never initialised.
- `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` (sdkconfig + defaults): TLS buffers in PSRAM. Settings `REQUIRES` gains
  `esp_http_client esp-tls mbedtls esp_netif pthread`.
- Partition table: unchanged. `partitions.csv` already has `otadata` + `ota_0` + `ota_1` (6 MB each, app is 3.1 MB).

### Status bar follow-up, Upgrade button width
- Battery percentage text at half the clock's size: the enabled Montserrat font whose line height is closest to half
  the clock font's (`half_size_font()` in Settings.cpp; Montserrat 14 with the current stylesheet).
- Battery icon first in the icon area, percentage right after it: [battery][%][Wi-Fi][SD][BT]. Brookesia builds
  [Wi-Fi][%][battery]; `applyBatteryPercentage()` re-orders once (`lv_obj_move_to_index`).
- `create_button()`: the label kept `lv_obj_set_pos(label, -1, 0)` from the old block. LVGL 9.2 leaves a centred child
  with x != 0 out of its parent's `LV_SIZE_CONTENT` width (`calc_content_width()`, circular-dependency guard), so the
  button was only as wide as its padding (about 3 letters). The offset is gone.
- `SettingsUpgrade.cpp`: two error messages printed the full `.part` path (up to 199 chars) into a 128-byte buffer,
  rejected by `-Werror=format-truncation`; they print `.temp/<file name>` now.

## Settings copy in flash, Wi-Fi station owned by Settings

### Settings copy in flash (`Settings.cpp`)
- Same JSON text as `/sdcard/.settings.json`, stored as NVS blob `settings`/`json` (existing `nvs` partition, no
  partition change).
- Boot (`ensureLoaded()`): SD file -> else flash copy -> else defaults, then `save()`.
- SD file that is not valid JSON: renamed `.settings.bad.json` (kept for the user), flash copy used, SD file rewritten
  from it. Before, a broken file was overwritten with defaults.
- `save()` (was `saveToSd()`): one JSON text, SD rewritten only when it changes (hash), flash only when it differs
  from the stored blob. SD write failure (card pulled out) still updates flash. `loadFromSd()` -> private
  `loadFromText()`. Buffer 4 -> 8 KB.

### Wi-Fi (`SettingsWifi.cpp`, new)
- JSON: `settings.wifi.disabled_on_boot`, `settings.wifi.ssid` (array), `settings.wifi.pass` (array, `false` = open
  network). Arrays of different lengths, more than 10 networks, an SSID not 1..32 chars or a pass not a string/false:
  whole list rejected (`ESP_LOGE`, status FAILED with the reason), arrays saved back untouched.
- One task `wifi_mgr` (4 KB internal stack, core 0) makes every `esp_wifi` call. Created on first need: Wi-Fi disabled
  or on standby costs no RAM.
- `enabled` false: nothing runs, `requestWifi()` returns false. `disabled_on_boot` false: connects at boot, stays
  connected. `disabled_on_boot` true: radio off until `requestWifi()`, off again 30 s after the last `releaseWifi()`.
- Pass: networks in order, next on disconnect event or no IP after 20 s. All failed: FAILED, radio off, until the next
  request or switch toggle. Link lost later: one new pass.
- Radio off = `esp_wifi_stop` + `esp_wifi_deinit` + netif destroyed: its internal RAM goes back to BLE.
- Start check: 56 KB internal free / 16 KB block (estimate). Log `Radio on: internal RAM A -> B free` gives the real
  cost.
- Status-bar icon: "no Wi-Fi" glyph unless connected; bars from RSSI (>= -60 / -72 dBm), sampled every 10 s.
- App API: `requestWifi()`, `waitWifi()`, `releaseWifi()`, `wifiStatus()`, `wifiStatusText()`, `wifiRssi()`.

### Users
- "Enable WiFi" switch -> `applyWifi()`.
- Firmware update check: `requestWifi()`, worker waits for the connection, `releaseWifi()` at the end. Message box
  shows `wifiStatusText()` on failure. Old `isWifiConnected()` (esp_netif lookup) replaced by the status.
- `ClockApp`: no forced light sleep while the radio is on (as with BLE); no sleep at all while an app holds a Wi-Fi
  request (download).

### Device name, Wi-Fi menu rows, no sleep during firmware work
- `settings.device.name` (max 31 bytes, cut on a UTF-8 boundary; default "S3 Watch"). A top-level `device.name` is
  read too, then saved under `settings`.
  - BLE: GAP name (`ble_mouse_init(cfg.device.name)`). Advertisement has 15 bytes left for the name: longer names go
    shortened there and complete in the scan response (29 bytes). Before, `ble_gap_adv_set_fields()` failed silently
    with names over 15 bytes.
  - Wi-Fi: DHCP hostname (router client list), set before each radio start. Hostname rules: letters, digits, '-';
    "SlimyRedstone's Watch" -> `SlimyRedstones-Watch`.
- `get/set_wifi_disable_on_boot_value()` ("Disable WiFi on boot" switch): off -> connect now (if Wi-Fi enabled),
  on -> standby, radio off 30 s after the last app releases it.
- `get_wifi_ip_address()`: "IP Address: 192.168.1.20", "IP Address: connecting..." or "IP Address: not connected".
  The status timer rewrites the row while the menu is open (label found by its prefix, pointer cleared on delete).
- `Settings::isFirmwareBusy()`: true for the whole update worker (Wi-Fi wait, check, download, future install).
  ClockApp: no light/deep sleep and no power-key deep sleep meanwhile (AXP2101 4 s hard power-off unchanged).

### Wi-Fi start `ESP_ERR_NO_MEM` (fix)
Log: `Init static rx buffer size: 2212`, `malloc buffer fail`, `Expected to init 10 rx buffer, actual is 5`, with
61 KB internal free (BLE up). The Wi-Fi MAC only DMAs from internal SRAM: static TX/RX buffers are internal whatever
`SPIRAM_TRY_ALLOCATE_WIFI_LWIP` says (`zalloc_internal_wrapper`, esp32s3/esp_adapter.c).
- `CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM` 16 -> 8 (~13 KB), `CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM` 10 -> 6 (~9 KB,
  still >= `RX_BA_WIN` 6), `CONFIG_ESP_WIFI_CSI_ENABLED` off (unused, ~1 KB per RX buffer). sdkconfig (both names) +
  sdkconfig.defaults.
- The `wifi_init: Failed to deinit Wi-Fi driver (0x3001)` lines are IDF's own clean-up inside the failed
  `esp_wifi_init()`, not an extra error.
- `ESP_ERR_NO_MEM` now shows "Not enough memory to start Wi-Fi (N KB free). Turn Bluetooth off and try again."

### BLE Mouse: no left/right swipe, wider scroll strip
- Brookesia `phone::App::Config::flags.disable_navigation_gesture_back` (new, default 0) + protected
  `disableNavigationGestureBack()`; `Manager::processGestureScreenChange()` then turns the left/right "back" swipe and
  its edge mask/indicator off for that app only. Bottom-edge swipe (home / recents) unchanged.
- BLEMouse: calls it in its constructor; its own edge-drag "back" in `onPadRelease()` removed.
- Scroll strip `SCROLL_STRIP_PX` 48 -> 58 (+20 %): drawn strip and touch zone.

### Sonometer: Options tab saved to `/sdcard/apps/sonometer.json`
- `{"fft": {"bars", "min_db", "max_db", "hide_peaks", "peaks"}}` = `n_bars_selector`, `fft_min_sens_slider`,
  `fft_max_sens_slider`, `fft_options_peaks_disabled_switch`, `fft_peaks_n_selector`.
- `run()`: file -> widgets (`params_setup()`) right after `Sonometer_ui_init()`, before `sound_graph_setup()` /
  `fft_options_setup()`, which read the widgets as before. Read on every opening: a file edited on a PC applies at the
  next one. Number not in a dropdown -> nearest option (warning); slider value clamped to its range; file rewritten
  with what is shown. Missing file/key: UI initial values, file written. Not JSON: renamed `sonometer.bad.json`.
- Changes (bar/peak selectors, peaks switch, both sliders) saved 1 s after the last one (`lv_timer`), and on `close()`.
- No SD card: values kept in RAM until restart. An unsaved change is not overwritten by the older file.
- Sonometer CMake: `REQUIRES ... json`.

### Sonometer: new Options UI (range slider, peaks grid)
- `fft_min_sens_slider` + `fft_max_sens_slider` -> `fft_sens_slider` (range mode, 0..90 dB in the UI): left knob =
  bottom of `fft_graph`, right knob = top. Label `fft_sens_value_label` through EEZ variable `fft_sensitivity_value`:
  "16dB to 54dB" (`get/set_var_fft_min|max_sensitivity_value` -> `get/set_var_fft_sensitivity_value`).
- `range_slider_set()`: LVGL 9.2 clamps the left knob to the right one and the right knob to the left one on every
  call, so the knob set first depends on the direction. Used for `apps/sonometer.json` (`min_db` / `max_db`, swapped
  if reversed). An older file with `min_db` below the new slider range (old default -48) is clamped to 0 and rewritten.
- `fft_peaks_list` as a 3-column grid (`FFT_PEAKS_GRID_COLS`), filled left to right then top to bottom: 3 x 4 for 10
  peaks. The EEZ export sets grid track *sizes* `{4}` / `{3}` (one 4 px row, one 3 px column in LVGL), replaced in
  `fft_peaks_grid_setup()` by 3 `LV_GRID_FR(1)` columns and one `LV_GRID_CONTENT` row per line. Cell padding 2 / 4 px
  (`FFT_PEAKS_CELL_PAD_VER/HOR`) so 4 rows fit the 100 px list.
- Cell text `"#%02u: %0.0fHz (%0.1fdB)"`, #01 = strongest; empty cell `"#04: -"`.

### Sonometer: readable peak cells, peak bars in colour
- Index removed: cell text back to `"%0.0fHz (%0.1fdB)"`, empty cell `"-"`.
- A scrolling label (list button default, `LV_LABEL_LONG_SCROLL_CIRCULAR`) restarts its scroll on every
  `lv_label_set_text()`, 4 times a second here: labels now `LV_LABEL_LONG_CLIP`. Wider cells so the text fits: list
  side padding 16 -> 4 px (`FFT_PEAKS_LIST_PAD_HOR`), cell side padding 4 -> 2 px: about 112 px per cell, against
  111 px for "1234Hz (56.7dB)" in Montserrat 14 (126 px for a 5-digit frequency: its end is clipped).
- Colours: cell i of n gets hue `FFT_PEAKS_HUE_FIRST` (0, red) + i * (240 - 0) / (n - 1) up to `FFT_PEAKS_HUE_LAST`
  (240, blue), saturation 70 % (pure blue is hard to read on the dark list), value 100 %. Text in that colour.
- Bars: each listed peak's bar (the bar holding the peak's bin centre, then its pixel column(s) as `fft_render` lays
  them out) is drawn in its cell's colour by `fft_draw_cb`: one opaque rectangle from the bar's current top
  (`s_fft_top`) to the bottom, over the white image. At most 10 rect draw tasks, no RGB plane rewrite in PSRAM.
  Updated with the list (every 250 ms); only changed columns are invalidated. Cleared when the peaks are hidden, the
  bar count or the peak count changes.

### Performance: Settings at rest, recents screen
- Settings 50-60 % "CPU" with nothing touched: every menu row label is `LV_LABEL_LONG_SCROLL_CIRCULAR`
  (`ui/screens.c` `create_text()`). The menu has a sidebar (`lv_menu_set_sidebar_page`), so both columns are narrow
  and long titles ("Disable sleep when charging", ...) overflow: each one scrolls for ever, its area re-rendered and
  flushed every 16 ms. `Settings::run()` now switches those labels to `LV_LABEL_LONG_WRAP` (`labels_stop_scrolling()`,
  ui/ untouched): long titles take 2 lines, nothing animates. (The perf monitor's CPU % is the LVGL task's busy time,
  flush DMA waits included.)
- Recents screen (swipe up from the bottom) under 1 FPS / 95 %: each app snapshot is the full 410x502 RGB565 screen
  shown at 40 % with `lv_image_set_scale()`; LVGL's software transform resampled it on every frame, for every visible
  app. Brookesia `RecentsScreenSnapshot` now shrinks it once per opening (box filter, `updateScaledSnapshot()`) into
  its own 163x200 draw buffer, drawn 1:1 (plain blit). Freed with the snapshot widget; formats other than RGB565 keep
  the scaled draw. The rounded corners (`clip_corner`, radius 20) stay: LVGL 9.2 only layers the 4 corner strips.
- Left as is: the snapshot itself (`lv_snapshot_take_to_draw_buf`, one off-screen render of the app when the recents
  screen opens) is a one-time hitch, not a per-frame cost.

### Settings menu fonts
- `ui/screens.c`: the screen and menu fonts used `LV_PART_MAIN | LV_PART_ITEMS` = `LV_PART_ITEMS` (`LV_PART_MAIN` is
  0, parts are values): labels inherit from `LV_PART_MAIN` only, so they always showed the theme font (Montserrat 14).
  Now `LV_PART_MAIN`: page contents 18 (menu font, inherited; the "Check for updates" button too), sidebar categories
  and "Settings" title (sidebar header) 16.
- Sidebar 30 % -> 39 % (159 px): "Developer" at 16 px + icon + paddings is 155 px, it wrapped before even at 14 px.
- Montserrat 8/10/12/16/20 were added to `sdkconfig.defaults` only; the existing `sdkconfig` had them as "is not set",
  which wins over the defaults (`lv_font_montserrat_16` would not exist). Enabled in `sdkconfig` too.
- Side effect: `half_size_font()` (battery percentage at half the clock size) now also picks among 8/10/12/16/20.

### Settings horizontal separator
- `create_h_separator()`: the line is 75 % of the parent's width, centred (12.5 % free on each side). The section is
  a flex column: `lv_obj_set_pos()` was ignored and margins take pixels only. The line now sits in a full-width
  transparent row (`lv_obj_remove_style_all`, not clickable/scrollable) and is aligned `TOP_MID` inside it.
- The end point is `LV_PCT(100)` (resolved by `lv_line` against its own width) instead of a fixed 410 px.

### Settings > Battery rows (`SettingsBattery.cpp`, new)
- Getters for the 6 rows (status, USB connected, battery voltage, USB voltage, est. life, est. life while sleeping;
  prototypes in `settings_ui_bridge.h`), rows found by prefix after each menu build and rewritten by the status timer
  (1.5 s) when the text changes. One PMU read shared by the getters (500 ms cache).
- Current and temperature rows removed (with their getters, the charging-session rate and the die-temperature read).
- The AXP2101 has no current measurement (no current ADC, no coulomb counter): the life rows use the mean current
  from the fuel gauge, % lost over time x 400 mAh (`BATTERY_CAPACITY_MAH`). 1 % = 4 mAh. "measuring..." until 5 min
  (awake) / 1 h (asleep) and 2 % steps were seen. On battery only.
- Awake / sleep rates kept apart, with exponential forgetting (2 h awake, 48 h asleep):
  - awake: gauge read every 10 s (status timer);
  - light sleep and its BLE/Wi-Fi stand-in: `batterySleepBegin()` / `batterySleepEnd()` in `ClockApp::enterLightSleep()`;
  - deep sleep / power off: `batteryCommit()` at the top of `ClockApp::enterDeepSleep()` saves the stats plus gauge %
    and wall-clock time to NVS (`settings`/`bat_stats`); the next boot counts the drop once the RTC sync set the clock.
- Saved every 15 min while awake too. No ULP: it could only re-read the same gauge %.
- Sleep life shows "> 12d04h" (lower bound) when fewer than 2 % were lost after 6 h of sleep.
- `pmu_open()`: VBUS voltage ADC channel enabled.

### Settings > Apps: apps loaded on boot (`SettingsApps.cpp`, new)
- `settings.apps.unloaded[]`: names of registered apps not loaded at boot (`cfg.apps`, up to 16 x 31 bytes). Missing =
  all loaded. Unknown names are kept (logged), non-strings ignored.
- `main.cpp`: `initAppFromRegistry()` replaced by a loop over `App::Registry::listRegisteredNames()` (creates no app):
  apps with `Settings::isAppLoadedOnBoot()` false are neither created nor installed (no icon, no `init()`).
  `Settings::ensureLoaded()` runs first. Changes apply at the next boot.
- App tags, set by the app in its .cpp with `SETTINGS_APP_TAGS(APP_NAME, tags)` (static init, known for apps never
  created; table in `SettingsApps.cpp`, `Settings::tagApp()` / `appTags()`):
  - `APP_TAG_HIDDEN`: not in `get_applications()`, so not on the Apps page;
  - `APP_TAG_ALWAYS_LOADED`: `set_application(name, false)` refused, `settings.apps.unloaded` entry ignored, row
    disabled if listed;
  - `APP_TAG_SYSTEM` = both. Set on Clock (sleep, wake-up, auto-start) and Settings (this list, PMU, Wi-Fi).
    Replaces the hard-coded always-loaded names.
- C bridge (`settings_ui_bridge.h`): `get_applications()` (every registered app, NULL-terminated, built once),
  `set_application(name, load)`, `get_application_status(name)`, `action_application_clicked(item)`.
- `ui/screens.c` `create_list(parent, get_items, on_click)`: `lv_list` (not scrollable, the page scrolls), one list
  button per item with a checkbox left of the label. A click on the item toggles the checkbox, then `on_click(item)`.
  Checkboxes start unchecked: `Settings::syncAppList()` sets them from cfg in `run()`.
- Side effects: BLE Mouse unloaded = no Bluetooth handler (the switch stays off). Podometer unloaded: its background
  service is still started by `main.cpp` (steps still counted).

## 5. What to do now

1. `idf.py build` (full build, link untested by me).
2. Flash, boot with an SD card that already has `.settings.json` (this used to corrupt the heap).
3. Exercise: Clock -> dim -> deep sleep -> button wake; Podometer ULP wake; BLE Mouse pairing; Recorder record/playback.
