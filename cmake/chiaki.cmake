###########################################
# Chiaki-lib configuration
###########################################
set(CHIAKI_IS_SWITCH ON CACHE BOOL "Building for Nintendo Switch")
set(CHIAKI_LIB_ENABLE_OPUS ON CACHE BOOL "Enable Opus audio codec")
set(CHIAKI_LIB_ENABLE_LIBNX_CRYPTO ON CACHE BOOL "Use libnx crypto backend")
set(CHIAKI_LIB_ENABLE_LIBNX_EXPERIMENTAL ON CACHE BOOL "Enable experimental PMULL GHASH")
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

if(CHIAKI_LIB_ENABLE_LIBNX_EXPERIMENTAL)
    target_compile_definitions(chiaki-lib PUBLIC CHIAKI_LIB_ENABLE_LIBNX_EXPERIMENTAL)
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/library/chiaki-ng/lib/src/crypto/libnx/ghash_pmull.c
        PROPERTIES COMPILE_FLAGS "-march=armv8-a+crypto"
    )
endif()
