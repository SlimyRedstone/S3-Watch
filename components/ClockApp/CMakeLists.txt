# ESP-IDF parses each component's CMakeLists.txt in CMake script mode for
# metadata extraction BEFORE the real configure step. CONFIGURE_DEPENDS is
# not allowed in script mode, so guard it with a check on CMAKE_PROJECT_NAME
# (only set during the real configure). New .c/.cpp files are picked up
# automatically during real builds; in script mode we still glob normally.
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
        ${CMAKE_CURRENT_LIST_DIR}/icon
        ${CMAKE_CURRENT_LIST_DIR}/fonts
    REQUIRES driver esp_hw_support XPowersLib Settings
    WHOLE_ARCHIVE   # Keep static plugin registrar linked.
)

set_source_files_properties(
    ${PROJ_SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
