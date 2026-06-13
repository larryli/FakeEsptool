# CMakeLists.txt - Build configuration for FakeEsptool
#
# Requires: CMake 3.20+, MSVC compiler
# Build (Release): mkdir build && cd build && cmake .. -G "NMake Makefiles" && cmake --build .
# Build (Debug):   mkdir build && cd build && cmake .. -G "NMake Makefiles" -DCMAKE_BUILD_TYPE=Debug && cmake --build .
# Build (Trace):   mkdir build && cd build && cmake .. -G "NMake Makefiles" -DENABLE_TRACE_FW=ON -DENABLE_TRACE_PROTO=ON && cmake --build .

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
    src/utils/deflate.c
    src/app_logview.c
    src/app_commands.c
    src/dlg/device_props.c
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
set_source_files_properties(src/resource.rc PROPERTIES COMPILE_FLAGS "/utf-8")

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
