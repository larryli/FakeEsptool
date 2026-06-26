# CMakeLists.txt - Build configuration for FakeEsptool
#
# Requires: CMake 3.20+, MSVC compiler
# Build (Release): cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build && cmake --build build --config Release -j
# Build (Debug):   cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -B build && cmake --build build --config Debug
# Build (Trace):   cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DENABLE_TRACE_FW=ON -DENABLE_TRACE_PROTO=ON -B build && cmake --build build --config Release -j
# Build (Agents):  cmake -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded -DLOG_NOT_SHOW_ASCII=ON -B build && cmake --build build --config Release -j

cmake_minimum_required(VERSION 3.20)
project(FakeEsptool C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Source files
add_executable(FakeEsptool WIN32
    src/main.c
    src/serial.c
    src/esptool/slip.c
    src/esptool/chip.c
    src/esptool/flash.c
    src/esptool/esptool.c
    src/esptool/device.c
    src/utils/config.c
    src/utils/lang.c
    src/utils/trace.c
    src/utils/mem.c
    src/utils/md5.c
    src/utils/deflate.c
    src/utils/encrypt.c
    src/app_logview.c
    src/app_commands.c
    src/dlg/device_props.c
    src/dlg/key_mgmt.c
    src/dlg/port_select.c
    src/dlg/about.c
    src/resource.rc
)

# Include directories
target_include_directories(FakeEsptool PRIVATE src)

# Disable automatic manifest generation (using custom manifest in resource.rc)
set_target_properties(FakeEsptool PROPERTIES
    LINK_FLAGS "/MANIFEST:NO"
)

# Link libraries
target_link_libraries(FakeEsptool PRIVATE
    comctl32
    uuid
    shlwapi
)

# Compiler options
target_compile_options(FakeEsptool PRIVATE /utf-8)

# Resource compiler options
set_source_files_properties(src/resource.rc PROPERTIES COMPILE_FLAGS "/utf-8 /nologo")

# Preprocessor definitions
target_compile_definitions(FakeEsptool PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

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
