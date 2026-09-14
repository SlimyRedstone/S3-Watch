# S3-Watch

Custom smartwatch firmware for the [Waveshare ESP32-S3-Touch-AMOLED-2.06](https://www.waveshare.com/esp32-s3-touch-amoled-2.06.htm?sku=31957).

Built on ESP-IDF, written in C and C++. The UI runs on LVGL through the
ESP-Brookesia phone system, so every feature is a self-contained app
installed into a launcher.

## Requirements

- ESP-IDF **v5.4.3** (exact version, other releases are untested)
- Waveshare ESP32-S3-Touch-AMOLED-2.06 board
- A microSD card (required by the Recorder and by persisted settings)
- Python 3 (used by the build to generate per-app UI symbols)

## Hardware

| Part | Chip | Used for |
|---|---|---|
| Display | SH8601 QSPI AMOLED, 410x502 | UI, backlight control |
| Touch | FT5x06 | Input, wake from light sleep |
| PMU | AXP2101 | Battery gauge, rail control across sleep |
| IMU | QMI8658C | Step detection |
| RTC | PCF85063A (I2C 0x51) | Battery-backed time across deep sleep |

## Apps

- **Clock**: the default app. Shows time and date pulled from the on-board
  RTC, and drives the power ladder (active, dimmed AOD, light sleep, deep
  sleep) based on idle time.
- **Podometer**: counts steps and tracks progress against an adjustable
  daily goal. Uses the ULP RISC-V co-processor so counting continues while
  the watch is in deep sleep. Steps and goal persist in NVS.
- **Recorder**: records speech to the SD card as 16-bit mono WAV at 44.1 kHz,
  with a live FFT visualizer. Recordings are listed and can be replayed.
- **Flashlight**: full-screen white or red light.
- **BLE Mouse**: acts as a Bluetooth LE trackpad to control a computer from
  afar. NimBLE HID over GATT.
- **BLE Media**: acts as a Bluetooth LE keyboard for media keys (play/pause,
  track skip, volume, mute) on a computer or phone. Shares the HID
  connection owned by BLE Mouse.
- **Settings** (WIP): manages watch parameters such as sleep timer, speaker
  volume, screen dimming and radio toggles. Config is stored on the SD card.
- **RemoteNow** (scaffold): UI shell for an ESP-NOW remote, no radio logic yet.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p <PORT> flash monitor
```

The board is configured for 16 MB flash with octal PSRAM at 80 MHz, and uses
a custom OTA partition table (`partitions.csv`).

## Project layout

```
main/            app_main, boot sequence, stylesheet
components/      one folder per app, plus shared libraries
  */ui/          LVGL UI generated from EEZ Studio projects
  Podometer/ulp/ ULP RISC-V firmware for deep-sleep step counting
  bsp_extra/     audio codec, I2S and player helpers on top of the BSP
  XPowersLib/    AXP2101 PMU driver
  brookesia_core/ vendored ESP-Brookesia UI framework
datasheets/      component datasheets and the board schematic
dev/             scaffolding and EEZ Studio helper scripts
```

### Developer tools

- `dev/new_app.py` scaffolds a new Brookesia app ready to host an EEZ Studio UI.
- `dev/prefix_eez.py` and the per-app `update_eez.py` prefix EEZ Studio global
  symbols so several apps can each ship their own `ui.c` without link
  collisions. Both run automatically at CMake configure time.

## Datasheets

The `datasheets/` folder holds the references used to write the drivers:
the AMOLED panel, AXP2101 PMU, PCF8563 RTC, QMI8658C IMU, and the board
schematic.

## Work in progress

- Better BLE management (random crashes for now)
- Faster boot time (currently ~0.5 s from deep sleep, 2 to 3 s from a full
  shutdown)
- Higher refresh rate and less screen tearing
- AirMouse
- Push notifications pulled from a connected phone
- Alarms
- Customizable backgrounds

## License

GPL-3.0. See [LICENSE](LICENSE).
