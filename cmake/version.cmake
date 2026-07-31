set(VERSION_MAJOR "0")
set(VERSION_MINOR "5")
set(VERSION_PATCH "3")

find_package(Git QUIET)

set(GIT_COMMIT "")
set(GIT_DIRTY "")
if (Git_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_COMMIT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
    execute_process(
        COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=no
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        OUTPUT_VARIABLE GIT_DIRTY
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if (GIT_COMMIT STREQUAL "")
    set(VERSION_REVISION "${VERSION_PATCH}")
else()
    if (NOT GIT_DIRTY STREQUAL "")
        set(GIT_COMMIT "${GIT_COMMIT}-dirty")
    endif()
    set(VERSION_REVISION "${VERSION_PATCH}-${GIT_COMMIT}")
endif()

set(APP_TITLE "Akira")
set(PROJECT_AUTHOR "xlanor")
set(PACKAGE_NAME "io.github.akira")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION}")

message(STATUS "Akira version: ${APP_VERSION}")
