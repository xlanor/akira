cmake_minimum_required(VERSION 3.13)

# Force Switch platform
set(PLATFORM_SWITCH ON CACHE BOOL "Build for Nintendo Switch" FORCE)

# Borealis paths
set(BOREALIS_DIR ${CMAKE_CURRENT_SOURCE_DIR}/library/borealis)
set(BOREALIS_LIBRARY ${BOREALIS_DIR}/library)

# Use deko3d for video rendering (must be set BEFORE commonOption.cmake)
set(USE_DEKO3D ON CACHE BOOL "Use deko3d for video rendering" FORCE)

option(DEBUG_DEKO3D_RENDERING "Enable debug squares in video shader" OFF)
option(MUTE_CHIAKI_LOGS "Mute chiaki library logs" OFF)

# Platform common options + toolchain from borealis
include(${BOREALIS_LIBRARY}/cmake/commonOption.cmake)
include(${BOREALIS_LIBRARY}/cmake/toolchain.cmake)

project(akira)

# Module search path: our cmake/ modules plus chiaki-ng's
list(APPEND CMAKE_MODULE_PATH
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake
    ${CMAKE_CURRENT_SOURCE_DIR}/library/chiaki-ng/cmake
)

# Project metadata + git-derived version
include(version)

# Platform options
list(APPEND APP_PLATFORM_OPTION
    -DBUILD_PACKAGE_NAME=${PACKAGE_NAME}
)

# Threads
find_package(Threads REQUIRED)
list(APPEND APP_PLATFORM_LIB ${CMAKE_THREAD_LIBS_INIT})

# Resources are embedded via --romfsdir in elf2nro
set(BRLS_RESOURCES_DIR "romfs:/")

# Dependencies (curl, ffmpeg, sdl2, ...) and the chiaki-ng library
include(dependencies)
include(chiaki)

# Vendored C libraries (wireguard target + lwIP sources)
include(vendored_libs)

###########################################
# Source files
###########################################
file(GLOB_RECURSE MAIN_SRC CONFIGURE_DEPENDS source/*.cpp)
list(APPEND MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/stream/nxext_ipc_server.c)
list(APPEND MAIN_SRC ${CMAKE_CURRENT_SOURCE_DIR}/source/core/pair/microecc/uECC.c)

# Switch wrapper for libnx initialization
list(APPEND MAIN_SRC ${BOREALIS_LIBRARY}/lib/platforms/switch/switch_wrapper.c)

# Sub-libraries
add_subdirectory(${BOREALIS_DIR}/library borealis)
add_subdirectory(library/tomlplusplus EXCLUDE_FROM_ALL)

###########################################
# Main target
###########################################
program_target(${PROJECT_NAME} "${MAIN_SRC}")
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 23)

target_sources(${PROJECT_NAME} PRIVATE ${LWIP_SRCS} ${LWIP_RELAY_SRCS})

target_include_directories(${PROJECT_NAME} PRIVATE
    include
    # Generated version header (configure_file in cmake/version.cmake)
    ${CMAKE_BINARY_DIR}/generated
    # Shared IPC definitions
    ${CMAKE_CURRENT_SOURCE_DIR}/overlay/common/include
    # Borealis includes
    ${BOREALIS_LIBRARY}/include
    # Real chiaki headers from chiaki-ng library
    ${CMAKE_CURRENT_SOURCE_DIR}/library/chiaki-ng/lib/include
    ${CMAKE_CURRENT_SOURCE_DIR}/library/chiaki-ng/lib/src
    ${CMAKE_CURRENT_BINARY_DIR}/chiaki-lib/include
    # WireGuard
    ${WIREGUARD_DIR}/include
    ${WIREGUARD_DIR}/src
    ${WIREGUARD_DIR}/lwip-relay/include
    # lwIP includes
    ${LWIP_DIR}/src/include
    ${APP_PLATFORM_INCLUDE}
)

target_compile_options(${PROJECT_NAME} PRIVATE
    -ffunction-sections
    -fdata-sections
    -Wunused-variable
    ${APP_PLATFORM_OPTION}
)

if (USE_DEKO3D)
    target_compile_definitions(${PROJECT_NAME} PRIVATE BOREALIS_USE_DEKO3D)
endif()

if (MUTE_CHIAKI_LOGS)
    target_compile_definitions(${PROJECT_NAME} PRIVATE MUTE_CHIAKI_LOGS)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE
    borealis
    chiaki-lib
    tomlplusplus::tomlplusplus
    ${SDL2_LIB}
    FFMPEG::avcodec
    FFMPEG::avutil
    FFMPEG::swscale
    ${SWRESAMPLE_LIB}
    ${DAV1D_LIB}
    ${JSONC_LIB}
    ${WIREGUARD_LIB}
    ${APP_PLATFORM_LIB}
)

add_dependencies(${PROJECT_NAME} wireguard_build)

# deko3d shader compilation + uam runtime link
include(deko3d_shaders)

# SDL2 depends on EGL/OpenGL libraries on Switch
if (PLATFORM_SWITCH)
    target_link_libraries(${PROJECT_NAME} PRIVATE EGL glapi drm_nouveau)
endif()

target_link_options(${PROJECT_NAME} PRIVATE ${APP_PLATFORM_LINK_OPTION})

# NRO packaging
include(packaging)
