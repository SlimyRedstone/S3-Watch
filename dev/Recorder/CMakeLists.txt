# Run update_eez.py at configure time so EEZ-Studio source files in ui/
# are renamed with this app's prefix before they are compiled. The script
# is idempotent, so re-running on every configure is safe.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_FOUND)
    execute_process(
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/update_eez.py
        WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
        RESULT_VARIABLE _eez_rc
        OUTPUT_QUIET
    )
    if(NOT _eez_rc EQUAL 0)
        message(WARNING "update_eez.py failed (rc=${_eez_rc}) at ${CMAKE_CURRENT_LIST_DIR}")
    endif()
else()
    message(WARNING "Python3 not found; skipping update_eez.py")
endif()
if(DEFINED CMAKE_PROJECT_NAME)    
    # Single recursive glob covers root, ui/, icon/, and any other subdir.
    # DO NOT add a second file(GLOB_RECURSE PROJ_SRCS_C ...) line — it would
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
    REQUIRES driver
    WHOLE_ARCHIVE
)
# Prefix common EEZ-Studio global symbols with this app's name so multiple
# apps each carrying their own ui.c/screens.c/actions.c can coexist without
# multiple-definition link errors.
# target_compile_definitions(${COMPONENT_LIB} PRIVATE
#     ui_init=Recorder_ui_init
#     ui_tick=Recorder_ui_tick
#     objects=Recorder_objects
#     flowState=Recorder_flowState
#     create_screens=Recorder_create_screens
#     tick_screens=Recorder_tick_screens
# )

set_source_files_properties(
    ${PROJ_SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
