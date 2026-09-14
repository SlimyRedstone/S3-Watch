# Prefix every EEZ Studio symbol per-component before any source is compiled.
# Lets each app keep its own ui_init / create_screens / objects without
# multiple-definition link errors.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/dev/prefix_eez.py
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        RESULT_VARIABLE _eez_rc
    )
    if(NOT _eez_rc EQUAL 0)
        message(WARNING "dev/prefix_eez.py failed (rc=${_eez_rc})")
    endif()
else()
    message(WARNING "Python3 not found; skipping dev/prefix_eez.py")
endif()


idf_component_register(
    SRCS 
        "main.cpp" 
        # "fonts/jetbrains_48_1bpp.c" 
        # "fonts/jetbrains_60_1bpp.c"
    INCLUDE_DIRS "."
    REQUIRES
        driver
        esp_netif
        esp_pm
        brookesia_core
        ClockApp
        Settings
        BLEMouse
        # BLEStress
        Flashlight
        BLEMultiMedia
        Recorder
        Podometer
        RemoteNow
)

target_compile_options(${COMPONENT_LIB} PUBLIC -Wno-missing-field-initializers)

# Function to get component library
function(get_component_library component_name output_var)
    # Get the exact component name
    idf_build_get_property(build_components BUILD_COMPONENTS)
    set(TARGET_COMPONENT "")
    foreach(COMPONENT ${build_components})
        if(COMPONENT MATCHES "${component_name}" OR COMPONENT MATCHES "espressif__${component_name}")
            set(TARGET_COMPONENT ${COMPONENT})
            break()
        endif()
    endforeach()

    # Get the component library
    if(TARGET_COMPONENT STREQUAL "")
        message(FATAL_ERROR "Component '${component_name}' not found.")
    else()
        idf_component_get_property(COMPONENT_LIB ${TARGET_COMPONENT} COMPONENT_LIB)
        set(${output_var} ${COMPONENT_LIB} PARENT_SCOPE)
    endif()
endfunction()

get_component_library("lvgl" LVGL_LIB)
target_compile_options(
    ${LVGL_LIB}
    PUBLIC
        "-Wno-attributes"
        # "-DCONFIG_BSP_DISPLAY_LVGL_AVOID_TEAR=true"
)
