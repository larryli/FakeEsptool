# CMakeLists.txt - Build configuration for tests
#
# Build: mkdir build_tests && cd build_tests
#        cmake .. -G "NMake Makefiles"
#        cmake --build .
#        ctest

cmake_minimum_required(VERSION 3.20)
project(FakeEsptoolTests C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Test executable: test_deflate
add_executable(test_deflate
    test_deflate.c
    ../src/utils/deflate.c
    ../src/utils/mem.c
)

# Include directories
target_include_directories(test_deflate PRIVATE ../src)

# Compiler options
target_compile_options(test_deflate PRIVATE /utf-8)

# Preprocessor definitions
target_compile_definitions(test_deflate PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# Test executable: test_slip
add_executable(test_slip
    test_slip.c
    ../src/fesptool/slip.c
)

# Include directories (stubs first to override real fesptool_hal.h)
target_include_directories(test_slip PRIVATE stubs ../src)

# Compiler options
target_compile_options(test_slip PRIVATE /utf-8)

# Preprocessor definitions
target_compile_definitions(test_slip PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# Test executable: test_encrypt
add_executable(test_encrypt
    test_encrypt.c
    ../src/utils/encrypt.c
)

# Include directories
target_include_directories(test_encrypt PRIVATE ../src)

# Compiler options
target_compile_options(test_encrypt PRIVATE /utf-8)

# Preprocessor definitions
target_compile_definitions(test_encrypt PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

# Enable testing
enable_testing()
add_test(NAME test_deflate COMMAND test_deflate)
add_test(NAME test_encrypt COMMAND test_encrypt)
add_test(NAME test_slip COMMAND test_slip)
