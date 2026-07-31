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
