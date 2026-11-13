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

#ifdef __cplusplus
}
#endif
