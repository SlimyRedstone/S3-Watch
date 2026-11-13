# To-be-updated for ESP-IDF v6.0.1 (not applied)

Source: official 5.5 -> 6.0 migration guide (v6.0.1 docs, ESP32-S3), v6.0.1 headers on GitHub, and a grep of this
tree. Ordered by how likely each item is to stop the build.

## Will break the build

1. **Warnings are errors by default** (`CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=n`) and **GCC 15.1** adds new warnings.
   - The EEZ-generated `components/Settings/ui/screens.h` emits `-Wunused-function` (static functions declared, never
     defined). `ui/` cannot be edited, so add `-Wno-unused-function` for that component in `components/Settings/CMakeLists.txt`,
     or set `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y` / `CONFIG_COMPILER_DISABLE_GCC15_WARNINGS=y`.
   - Expect new hits from `-Wunterminated-string-initialization`, `-Wdangling-reference`, `-Wheader-guard` in
     brookesia_core, XPowersLib and generated UI code.
2. **Legacy I2S driver removed** (`driver/i2s.h`). `managed_components/espressif__esp_codec_dev` 1.3.6
   (`platform/audio_codec_data_i2s.c`) and its legacy-I2C control path (`audio_codec_ctrl_i2c.c`) will not build.
   The Waveshare BSP (`waveshare/esp32_s3_touch_amoled_2_06` 1.0.6) pins `esp_codec_dev ~1.3.1`, `esp_lvgl_port ^2`,
   `waveshare/esp_lcd_sh8601 2.0.0`. All three need 6.0-compatible releases or a local fork in `components/`.
3. **`driver` component no longer re-exports** `esp_driver_i2s`, `esp_driver_ledc`, `esp_driver_uart`, `esp_driver_spi`,
   `esp_driver_sdmmc`, ... (18 components). `esp_driver_gpio` and `esp_driver_i2c` are still pulled in.
   - `components/bsp_extra/CMakeLists.txt`: add `esp_driver_i2s esp_driver_ledc` to `REQUIRES`.
   - Any other component that includes `driver/i2s_std.h` / `driver/ledc.h` via `REQUIRES driver`: same.
   - Public driver headers no longer include FreeRTOS headers: add `freertos/FreeRTOS.h`, `freertos/task.h`,
     `freertos/queue.h`, `freertos/semphr.h` explicitly where they were inherited (`esp_event.h` too).
4. **esp_lcd API changes** hit the BSP and `esp_lcd_sh8601`: GPIO fields are `gpio_num_t`, `color_space`/`rgb_endian`
   removed (`rgb_ele_order`), `esp_lcd_panel_disp_off` removed, legacy-I2C LCD IO removed. Third-party, needs updated versions.
5. **Linker orphan sections are errors.** Check the first link; the ULP embed, `RTC_DATA_ATTR`, `WHOLE_ARCHIVE` plugin
   registrars and EEZ assets are the candidates. Fallback: `CONFIG_COMPILER_ORPHAN_SECTIONS=warning`.
6. **esp-idf-kconfig v3 syntax.** Re-validate `components/bsp_extra/Kconfig` and `components/brookesia_core/Kconfig`.
7. **`esp_vfs_fat_sdmmc_unmount` removed** (use `esp_vfs_fat_sdcard_unmount`): used by the BSP's `bsp_sdcard_unmount`.
8. Managed-component `idf` version constraints: re-run the component manager; `esp-lib-utils 0.3.x`, `esp-boost 0.3.x`,
   `chmorgan/esp-audio-player 1.0.x`, `esp-dsp 1.4.x` need releases that declare `idf >= 6.0` support.

## Deprecated now, remove before 7.0 (project code)

| Where | Current | Replace with |
|---|---|---|
| `ClockApp.cpp:602`, `Podometer.cpp:712` | `esp_sleep_enable_ext1_wakeup(mask, ESP_EXT1_WAKEUP_ANY_LOW)` | `esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW)` (doc note: "will be deprecated in v6.0") |
| `ClockApp.cpp:337`, `Podometer.cpp:895,1126,1164` | `esp_sleep_get_wakeup_cause()` (deprecated in 6.0) | `esp_sleep_get_wakeup_causes()` returns a bitmask: test `causes & BIT(ESP_SLEEP_WAKEUP_ULP)` etc. |
| `main.cpp` (timer lambda), `ClockApp.cpp` | `t->user_data`, `touch->state` (LVGL private) | `lv_timer_get_user_data(t)`, `lv_indev_get_state(touch)`; needed when LVGL is bumped past 9.2 |
| `components/*/CMakeLists.txt` | `REQUIRES driver` | `REQUIRES esp_driver_gpio esp_driver_i2c ...` per component (the umbrella still exists in 6.0 but only wraps the legacy I2C/touch/TWAI drivers) |

## Behaviour changes to verify on hardware

- **Default libc is PicolibC** (`CONFIG_LIBC_PICOLIBC`). `stdin/stdout/stderr` are global, not per task.
  Check: `itoa()` in `Podometer.cpp:787` (non-standard; present in newlib, verify in picolibc or replace with `snprintf`),
  `%f` formatting in Podometer/Recorder logs and CSV (picolibc float printf is a Kconfig option), `strcasecmp`, `mktime`
  with `tm_isdst = -1`. Alternative: keep `CONFIG_LIBC_NEWLIB=y`.
- **Global constructor order is now ascending.** The app launcher order comes from `ESP_UTILS_REGISTER_PLUGIN_WITH_CONSTRUCTOR`
  static registrars in each component; app order in the registry (and therefore `inited_apps`) may change.
  `main.cpp` looks apps up by name, so autostart is safe.
- **I2C NACK now returns `ESP_ERR_INVALID_RESPONSE`** instead of `ESP_ERR_INVALID_STATE`. Project code only compares
  against `ESP_OK`; XPowersLib callbacks likewise. No change needed, but any future error-specific handling must use the new code.
- `CONFIG_COMPILER_ASSERT_NDEBUG_EVALUATE` defaults to `n`: `assert()` arguments are no longer evaluated when asserts are
  off. `bsp_board_extra.c` asserts have no side effects, fine.
- `CONFIG_FATFS_USE_DYN_BUFFERS=y` and `CONFIG_VFS_SUPPORT_TERMIOS=n` by default. Recorder/SD paths unaffected unless termios is used.
- RTC/`esp32s3/rtc.h` removed: not included by project code; XPowersLib and BSP do not include it either.
- `CONFIG_LOG_VERSION` may move to V2 in a later release; `ESP_UTILS_LOG*` macros from esp-lib-utils wrap `ESP_LOG*`, fine.

## Tooling

- GCC 15.1 toolchain and Python >= 3.10 (install via `idf_tools.py install` for 6.0).
- CMake >= 3.22 (project now says 3.16 minimum, fine).
- `.vscode/settings.json` still points at the 5.4.3 tree for IntelliSense.

## Suggested order

1. Bump managed components first (BSP, esp_codec_dev, esp_lvgl_port, esp_lcd_sh8601, esp_lcd_touch*) to versions
   that declare 6.0 support; fork into `components/` if none exist.
2. Fix `REQUIRES` and explicit FreeRTOS includes.
3. Build with `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y` once to see the full warning list, then fix and re-enable.
4. Replace the four sleep API call sites.
5. Decide newlib vs picolibc, then test sleep/ULP/BLE on hardware.
