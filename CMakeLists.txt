cmake_minimum_required(VERSION 3.5.0)
project(FakeEsptool VERSION 0.1.0 LANGUAGES C)

if(MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(FakeEsptool WIN32 main.c SerialPort.c main.rc)
target_link_libraries(FakeEsptool comctl32 comdlg32)
