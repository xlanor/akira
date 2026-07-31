# Build NRO
set(BUILD_FONT_DIR ${CMAKE_BINARY_DIR}/resources/font)
add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE} --create "${APP_TITLE}" "${PROJECT_AUTHOR}" "${APP_VERSION_NACP}" ${PROJECT_NAME}.nacp
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
    COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${CMAKE_BINARY_DIR}/resources
    ALL
)
