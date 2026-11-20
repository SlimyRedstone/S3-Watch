if(DEFINED CMAKE_PROJECT_NAME)
    file(GLOB_RECURSE PROJ_SRCS_C   CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.c)
    file(GLOB_RECURSE PROJ_SRCS_CPP CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
else()
    file(GLOB_RECURSE PROJ_SRCS_C   ${CMAKE_CURRENT_LIST_DIR}/*.c)
    file(GLOB_RECURSE PROJ_SRCS_CPP ${CMAKE_CURRENT_LIST_DIR}/*.cpp)
endif()

idf_component_register(
    SRCS ${PROJ_SRCS_C} ${PROJ_SRCS_CPP}
    INCLUDE_DIRS 
        ${CMAKE_CURRENT_LIST_DIR}
        ${CMAKE_CURRENT_LIST_DIR}/ui
        ${CMAKE_CURRENT_LIST_DIR}/icon
    REQUIRES driver esp_hw_support esp_timer XPowersLib json fatfs bsp_extra
             esp_http_client esp-tls mbedtls esp_netif pthread   # firmware update check (SettingsUpgrade.cpp)
    WHOLE_ARCHIVE   # Keep static plugin registrar linked.
)

set_source_files_properties(
    ${PROJ_SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)

# The EEZ export in ui/ calls get_sleep_charge_button_value() without a
# prototype (missing from the generated actions.h). ui/ must not be edited,
# so inject the declaration at compile time. See settings_ui_bridge.h.
set_source_files_properties(
    ${CMAKE_CURRENT_LIST_DIR}/ui/screens.c
    PROPERTIES
        COMPILE_FLAGS "-include ${CMAKE_CURRENT_LIST_DIR}/settings_ui_bridge.h"
)
