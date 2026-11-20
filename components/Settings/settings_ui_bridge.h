/*
 * Prototypes the EEZ Studio export forgot.
 *
 * ui/screens.c calls get_sleep_charge_button_value() but the generated
 * ui/actions.h does not declare it (nor its setter). C treats an implicit
 * function declaration as an error since GCC 14. The ui/ folder is
 * generated and must not be edited, so this header is force-included into
 * ui/screens.c from CMakeLists.txt (-include) and also pulled into
 * Settings.cpp so the definitions match these prototypes.
 *
 * If a future EEZ export adds these two prototypes to actions.h, this file
 * and the -include line in CMakeLists.txt can be deleted.
 */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool get_sleep_charge_button_value(void);
void set_sleep_charge_button_value(bool value);

// Battery > "Display percentage" (also missing from actions.h)
bool get_battery_perc_text_value(void);
void set_battery_perc_text_value(bool value);

// Developer > "Enable Performance counter" (also missing from actions.h)
bool get_developer_performance_value(void);
void set_developer_performance_value(bool value);

// WiFi > "Disable WiFi on boot" and the IP address row (also missing from actions.h)
bool get_wifi_disable_on_boot_value(void);
void set_wifi_disable_on_boot_value(bool value);
const char *get_wifi_ip_address(void);           // "IP Address: 192.168.1.20"

// Battery rows (SettingsBattery.cpp). Static buffers, kept current by the status timer.
const char *get_battery_charge_status_label(void);        // "Status: CHARGING"
const char *get_battery_usb_status_label(void);           // "USB connected: true"
const char *get_battery_voltage_label(void);              // "Voltage: 4123mV"
const char *get_battery_ac_voltage_label(void);           // "USB voltage: 5120mV"
const char *get_battery_estimated_life_label(void);       // "Est. life: 1h35"
const char *get_battery_estimated_sleep_life_label(void); // "Est. life, while sleeping: 5h45"

#ifdef __cplusplus
}
#endif
