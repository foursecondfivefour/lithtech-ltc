cmake_minimum_required(VERSION 3.20)
project(lithtech_ltc CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_library(ltc_core STATIC
    src/app.cpp
    src/command_line.cpp
    src/compressed_stream.cpp
    src/file.cpp
    src/lzss_decoder.cpp
    src/lzss_window.cpp
    src/paths.cpp
)
target_include_directories(ltc_core PUBLIC include)
# новый код порта собирается без предупреждений (оригинал — отдельно, в ref-build)
target_compile_options(ltc_core PRIVATE -Wall -Wextra -Wpedantic -Werror)

add_executable(ltc src/main.cpp)
target_link_libraries(ltc PRIVATE ltc_core)



# у каждого тестового файла свой main -> собираем отдельные бинарники


add_executable(ltc_tests_m1 tests/test_m1.cpp)
target_link_libraries(ltc_tests_m1 PRIVATE ltc_core)

add_executable(ltc_tests_m2 tests/test_m2.cpp)
target_link_libraries(ltc_tests_m2 PRIVATE ltc_core)

add_executable(ltc_tests_m3 tests/test_m3.cpp)
target_link_libraries(ltc_tests_m3 PRIVATE ltc_core)

enable_testing()
add_test(NAME unit_m1 COMMAND ltc_tests_m1)
add_test(NAME unit_m2 COMMAND ltc_tests_m2)
add_test(NAME unit_m3 COMMAND ltc_tests_m3)
