cmake_minimum_required(VERSION 3.13)

# Force Switch platform
set(PLATFORM_SWITCH ON CACHE BOOL "Build for Nintendo Switch" FORCE)

# Borealis paths
set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Use deko3d for video rendering (must be set BEFORE commonOption.cmake)
set(USE_DEKO3D ON CACHE BOOL "Use deko3d for video rendering" FORCE)

# Debug option for deko3d rendering (shows colored squares in corners)
option(DEBUG_DEKO3D_RENDERING "Enable debug squares in video shader" OFF)

# Include Platform common options from borealis
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)

# Include toolchain configuration
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

# Project Info
project(akira)
set(VERSION_MAJOR "0")
set(VERSION_MINOR "1")
set(VERSION_REVISION "5")
set(APP_TITLE "Akira")
set(PROJECT_AUTHOR "xlanor")
set(PACKAGE_NAME "io.github.akira")
set(PROJECT_ICON ${CMAKE_CURRENT_SOURCE_DIR}/resources/img/icon.jpg)
set(PROJECT_RESOURCES ${CMAKE_CURRENT_SOURCE_DIR}/resources)
set(APP_VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REVISION}")

# Platform options
list(APPEND APP_PLATFORM_OPTION
    -DBUILD_PACKAGE_NAME=${PACKAGE_NAME}
    -DBUILD_VERSION_MAJOR=${VERSION_MAJOR}
    -DBUILD_VERSION_MINOR=${VERSION_MINOR}
    -DBUILD_VERSION_REVISION=${VERSION_REVISION}
)

# Find threads
find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

# Resources are embedded via --romfsdir in elf2nro
set(BRLS_RESOURCES_DIR "romfs:/")

# Add chiaki-ng cmake modules to path 
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/library/chiaki-ng/cmake)

###########################################
# Find dependencies for Switch
###########################################
if (PLATFORM_SWITCH)
    find_package(PkgConfig REQUIRED)

    # Find curl (will find our custom curl-libnx that was installed first)
    # MUST be before chiaki-lib since it requires CURL::libcurl target
    pkg_search_module(CURL REQUIRED libcurl)
    message(STATUS "Found libcurl: ${CURL_VERSION}")
    message(STATUS "  Include: ${CURL_INCLUDE_DIRS}")
    message(STATUS "  Libraries: ${CURL_STATIC_LIBRARIES}")
    message(STATUS "  Library dirs: ${CURL_LIBRARY_DIRS}")

    # Find the actual libcurl.a file
    find_library(CURL_LIBRARY curl PATHS ${CURL_LIBRARY_DIRS} NO_DEFAULT_PATH)
    if(NOT CURL_LIBRARY)
        # Fallback to first library dir
        list(GET CURL_LIBRARY_DIRS 0 CURL_FIRST_LIB_DIR)
        set(CURL_LIBRARY "${CURL_FIRST_LIB_DIR}/libcurl.a")
    endif()
    message(STATUS "  Library: ${CURL_LIBRARY}")

    # Find zstd (required by curl for content encoding) - must be found before CURL::libcurl
    find_library(ZSTD_LIB zstd)
    message(STATUS "  zstd: ${ZSTD_LIB}")

    # Create CURL::libcurl imported target for chiaki-lib compatibility
    add_library(CURL::libcurl STATIC IMPORTED)
    set_target_properties(CURL::libcurl PROPERTIES
        IMPORTED_LOCATION "${CURL_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${CURL_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${ZSTD_LIB}"
    )

    list(APPEND APP_PLATFORM_INCLUDE ${CURL_INCLUDE_DIRS})

    # Find FFmpeg components
    find_package(FFMPEG REQUIRED COMPONENTS avcodec avutil swscale)

    # Find SDL2
    find_library(SDL2_LIB SDL2)
    if(NOT SDL2_LIB)
        message(FATAL_ERROR "SDL2 not found")
    endif()

    # Find swresample
    find_library(SWRESAMPLE_LIB swresample)

    # Find json-c for PSN API
    find_library(JSONC_LIB json-c PATHS ${PORTLIBS}/lib)
    if(JSONC_LIB)
        list(APPEND APP_PLATFORM_INCLUDE ${PORTLIBS}/include/json-c)
    endif()

    # Find dav1d (AV1 decoder)
    find_library(DAV1D_LIB dav1d)
endif()

###########################################
# Chiaki-lib configuration
###########################################
set(CHIAKI_IS_SWITCH ON CACHE BOOL "Building for Nintendo Switch")
set(CHIAKI_LIB_ENABLE_OPUS ON CACHE BOOL "Enable Opus audio codec")
set(CHIAKI_LIB_ENABLE_LIBNX_CRYPTO ON CACHE BOOL "Use libnx crypto backend")
set(CHIAKI_ENABLE_FFMPEG_DECODER ON CACHE BOOL "Enable FFmpeg video decoder")
set(CHIAKI_ENABLE_TESTS OFF CACHE BOOL "Disable tests")

# Use bundled nanopb and jerasure (required by chiaki-lib)
set(CHIAKI_USE_SYSTEM_NANOPB OFF CACHE BOOL "Use bundled nanopb")
set(CHIAKI_USE_SYSTEM_JERASURE OFF CACHE BOOL "Use bundled jerasure")
set(CHIAKI_ENABLE_STEAM_SHORTCUT OFF CACHE BOOL "Disable Steam shortcut")

# Find Python for nanopb generator
find_package(PythonInterp 3 REQUIRED)

# Add third-party dependencies (nanopb, jerasure)
add_subdirectory(library/chiaki-ng/third-party chiaki-third-party)

# Add chiaki-ng library
add_subdirectory(library/chiaki-ng/lib chiaki-lib)

# Add compile definitions for Switch platform (must be after add_subdirectory)
if(PLATFORM_SWITCH AND CHIAKI_LIB_ENABLE_LIBNX_CRYPTO)
    target_compile_definitions(chiaki-lib PUBLIC CHIAKI_LIB_ENABLE_LIBNX_CRYPTO)
    target_compile_definitions(chiaki-lib PUBLIC CHIAKI_IS_SWITCH)
    target_compile_definitions(chiaki-lib PUBLIC __SWITCH__)
endif()

###########################################
# Source files
###########################################
file(GLOB_RECURSE MAIN_SRC source/*.cpp)

# Switch wrapper for libnx initialization
list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)

# Add borealis library
add_subdirectory(${BOREALIS_DIR}/library borealis)

# Main target
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17)

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    include
    # Borealis includes
    ${BOREALIS_LIBRARY}/include
    # Real chiaki headers from chiaki-ng library
    ${CMAKE_CURRENT_SOURCE_DIR}/library/chiaki-ng/lib/include
    ${CMAKE_CURRENT_BINARY_DIR}/chiaki-lib/include
    ${APP_PLATFORM_INCLUDE}
)

# Compile options
target_compile_options(${PROJECT_NAME} PRIVATE
    -ffunction-sections
    -fdata-sections
    -Wunused-variable
    ${APP_PLATFORM_OPTION}
)

# Add deko3d compile definition when enabled
if (USE_DEKO3D)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BOREALIS_USE_DEKO3D)
endif()

# Add mute chiaki logs option
option(MUTE_CHIAKI_LOGS "Mute chiaki library logs" OFF)
if (MUTE_CHIAKI_LOGS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MUTE_CHIAKI_LOGS)
endif()

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    chiaki-lib
    ${SDL2_LIB}
    FFMPEG::avcodec
    FFMPEG::avutil
    FFMPEG::swscale
    ${SWRESAMPLE_LIB}
    ${DAV1D_LIB}
    ${JSONC_LIB}
    ${APP_PLATFORM_LIB}
)

# SDL2 depends on EGL/OpenGL libraries on Switch
if (PLATFORM_SWITCH)
    target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau)
endif()

# Link options
target_link_options(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_LINK_OPTION})

# Compile deko3d shaders
if (USE_DEKO3D)
    find_program(NX_UAM_EXE NAMES uam HINTS "${DEVKITPRO}/tools/bin")
    if (NOT NX_UAM_EXE)
        message(FATAL_ERROR "Could not find uam: try installing uam")
    endif()

    set(SHADER_DIR "${PROJECT_RESOURCES}/shaders")
    set(SHADER_OUT_DIR "${CMAKE_BINARY_DIR}/resources/shaders")

    # Compile borealis/nanovg shaders
    gen_dksh("${SHADER_OUT_DIR}")

    # Compile video shaders
    file(MAKE_DIRECTORY "${SHADER_OUT_DIR}")

    # Build shader defines based on options
    set(SHADER_DEFINES "")
    if (DEBUG_DEKO3D_RENDERING)
        list(APPEND SHADER_DEFINES "-DDEBUG_DEKO3D_RENDERING")
        message(STATUS "DEBUG_DEKO3D_RENDERING enabled - debug squares will be visible")
    endif()

    execute_process(COMMAND ${NX_UAM_EXE} -s vert -o "${SHADER_OUT_DIR}/video_vsh.dksh" "${SHADER_DIR}/video_vsh.glsl" TIMEOUT 5)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag ${SHADER_DEFINES} -o "${SHADER_OUT_DIR}/video_fsh.dksh" "${SHADER_DIR}/video_fsh.glsl" TIMEOUT 5)
    execute_process(COMMAND ${NX_UAM_EXE} -s vert -o "${SHADER_OUT_DIR}/text_vsh.dksh" "${SHADER_DIR}/text_vsh.glsl" TIMEOUT 5)
    execute_process(COMMAND ${NX_UAM_EXE} -s frag -o "${SHADER_OUT_DIR}/text_fsh.dksh" "${SHADER_DIR}/text_fsh.glsl" TIMEOUT 5)
    message(STATUS "Compiled video and text shaders to ${SHADER_OUT_DIR}")
endif()

# Build NRO
set(BUILD_FONT_DIR ${CMAKE_BINARY_DIR}/resources/font)
add_custom_target(${PROJECT_NAME}.nro
    DEPENDS ${PROJECT_NAME}
    COMMAND ${NX_NACPTOOL_EXE} --create "${APP_TITLE}" "${PROJECT_AUTHOR}" "${APP_VERSION}" ${PROJECT_NAME}.nacp
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_RESOURCES} ${CMAKE_BINARY_DIR}/resources
    COMMAND ${NX_ELF2NRO_EXE} ${PROJECT_NAME}.elf ${PROJECT_NAME}.nro
        --icon=${PROJECT_ICON}
        --nacp=${PROJECT_NAME}.nacp
        --romfsdir=${CMAKE_BINARY_DIR}/resources
    ALL
)
