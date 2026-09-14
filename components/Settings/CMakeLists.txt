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
    REQUIRES driver esp_hw_support XPowersLib json fatfs
    WHOLE_ARCHIVE   # Keep static plugin registrar linked.
)

set_source_files_properties(
    ${PROJ_SRCS_CPP}
    PROPERTIES
        COMPILE_FLAGS "-Wno-missing-field-initializers"
)
