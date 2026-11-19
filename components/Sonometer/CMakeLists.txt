# EEZ Studio symbols in ui/ are prefixed with "Sonometer_" at configure time by the
# project-wide dev/prefix_eez.py (run from main/CMakeLists.txt), so this app can keep
# its own ui_init / objects / create_screens next to the other apps.
if(DEFINED CMAKE_PROJECT_NAME)
    # Single recursive glob covers root, ui/, icon/, and any other subdir.
    # DO NOT add a second file(GLOB_RECURSE PROJ_SRCS_C ...) line: it would
    # OVERWRITE this variable, not append, and you would lose source files.
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
    REQUIRES driver bsp_extra Settings
    WHOLE_ARCHIVE
)

set_source_files_properties(
    ${PROJ_SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
