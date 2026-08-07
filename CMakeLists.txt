# CMakeLists.txt - Build configuration for FakeEsptool
#
# Supported toolchains:
#   - MSVC (Visual Studio Build Tools)   : any CMake >= 3.20
#   - Pelles C (compiler id "PellesC")    : CMake >= 4.4.0 (native support added in 4.4)
#
# The toolchain is auto-detected; no extra flags are required to select it.
#
# Both toolchains use the default "build" directory. Pick ONE toolchain per
# workspace. If you switch toolchains, remove the build directory first:
#   rmdir /s /q build
# (CMake cannot change the generator inside an existing build directory.)
#
# MSVC (multi-config generator):
#   cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build
#   cmake --build build --config Release -j
#   cmake --build build --config Debug
#
# Pelles C (single-config generator, CMake >= 4.4.0 + Ninja):
#   cmake -G Ninja -DCMAKE_BUILD_TYPE=Release -B build
#   cmake --build build


cmake_minimum_required(VERSION 3.20)
project(FakeEsptool C)

# Pelles C gained native CMake support in 4.4.0 (compiler id "PellesC").
if(CMAKE_C_COMPILER_ID STREQUAL "PellesC" AND CMAKE_VERSION VERSION_LESS 4.4.0)
    message(FATAL_ERROR "Pelles C builds require CMake >= 4.4.0 (found ${CMAKE_VERSION}).")
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable C11 safe functions (_s suffix)
add_compile_definitions(__STDC_WANT_LIB_EXT1__=1)

# Source files
add_executable(FakeEsptool WIN32
    src/main.c
    src/serial.c
    src/device_file.c
    src/fesptool_hal.c
    src/fesptool/slip.c
    src/fesptool/chip.c
    src/fesptool/efuse.c
    src/fesptool/flash.c
    src/fesptool/esptool.c
    src/utils/config.c
    src/utils/lang.c
    src/utils/trace.c
    src/utils/mem.c
    src/utils/md5.c
    src/utils/deflate.c
    src/utils/encrypt.c
    src/app_logview.c
    src/app_commands.c
    src/app_protocol.c
    src/dlg/device_props.c
    src/dlg/key_mgmt.c
    src/dlg/port_select.c
    src/dlg/about.c
    src/resource.rc
)

# Include directories
target_include_directories(FakeEsptool PRIVATE src)

# Preprocessor definitions (shared by all toolchains)
target_compile_definitions(FakeEsptool PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# --- Toolchain specific settings -------------------------------------------

if(MSVC)
    # MSVC: disable automatic manifest (custom manifest lives in resource.rc)
    set_target_properties(FakeEsptool PROPERTIES
        LINK_FLAGS "/MANIFEST:NO"
    )

    # MSVC compiler options
    target_compile_options(FakeEsptool PRIVATE /utf-8)

    # MSVC resource compiler options
    set_source_files_properties(src/resource.rc PROPERTIES COMPILE_FLAGS "/utf-8 /nologo")

    # Link libraries (MSVC implicitly links kernel32/user32/gdi32)
    target_link_libraries(FakeEsptool PRIVATE
        comctl32
        uuid
        shlwapi
    )

    message(STATUS "Toolchain: MSVC (${CMAKE_C_COMPILER_VERSION})")
elseif(CMAKE_C_COMPILER_ID STREQUAL "PellesC")
    # Pelles C: full set of system import libraries (mirrors FakeEsptool.ppj)
    target_link_libraries(FakeEsptool PRIVATE
        kernel32
        user32
        gdi32
        comctl32
        comdlg32
        advapi32
        setupapi
        shlwapi
        uuid
        Shell32
        Version
    )

    # Pelles C resource compiler (porc) does not accept MSVC-style flags.
    # Enable UTF-8 via the source encoding definition instead of rc flags.
    message(STATUS "Toolchain: Pelles C (${CMAKE_C_COMPILER_VERSION})")
else()
    message(WARNING "Unsupported toolchain (${CMAKE_C_COMPILER_ID}); build may fail.")
endif()

# --- Build type defaults ----------------------------------------------------

# Align Debug behavior with FakeEsptool.ppj, which enables trace logging in Debug.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# --- Feature options --------------------------------------------------------

# Trace logging options (enable one or both)
option(ENABLE_TRACE_FW "Enable framework trace logging" OFF)
option(ENABLE_TRACE_PROTO "Enable protocol trace logging" OFF)

if(ENABLE_TRACE_FW)
    target_compile_definitions(FakeEsptool PRIVATE ENABLE_TRACE_FW=1)
    message(STATUS "Framework trace enabled")
endif()

if(ENABLE_TRACE_PROTO)
    target_compile_definitions(FakeEsptool PRIVATE ENABLE_TRACE_PROTO=1)
    message(STATUS "Protocol trace enabled")
endif()

# Log display options
option(LOG_NOT_SHOW_ASCII "Hide ASCII decode in RX/TX hex dump" OFF)

if(LOG_NOT_SHOW_ASCII)
    target_compile_definitions(FakeEsptool PRIVATE LOG_NOT_SHOW_ASCII=1)
    message(STATUS "ASCII decode in hex dump disabled")
endif()

# Memory debug options
option(ENABLE_MEM_DEBUG "Enable memory allocation leak tracking" OFF)

if(ENABLE_MEM_DEBUG)
    target_compile_definitions(FakeEsptool PRIVATE ENABLE_MEM_DEBUG=1)
    message(STATUS "Memory leak tracking enabled")
endif()

# Debug configuration: enable trace logging by default (matches .ppj Debug mode)
if((CMAKE_BUILD_TYPE STREQUAL "Debug") OR (CMAKE_CONFIGURATION_TYPES AND "Debug" IN_LIST CMAKE_CONFIGURATION_TYPES))
    if(NOT ENABLE_TRACE_FW)
        target_compile_definitions(FakeEsptool PRIVATE ENABLE_TRACE_FW=1)
        message(STATUS "Framework trace enabled (Debug default)")
    endif()
    if(NOT ENABLE_TRACE_PROTO)
        target_compile_definitions(FakeEsptool PRIVATE ENABLE_TRACE_PROTO=1)
        message(STATUS "Protocol trace enabled (Debug default)")
    endif()
endif()
