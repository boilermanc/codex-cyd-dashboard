cmake_minimum_required(VERSION 3.20)
project(esp32_codex_notifications_tests LANGUAGES CXX)

enable_testing()

add_executable(ui-model-test
  src/ui-model.cpp
  test/ui-model-test.cpp
)

target_include_directories(ui-model-test PRIVATE include)
target_compile_features(ui-model-test PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(ui-model-test PRIVATE /W4 /WX /wd4127 /wd4244 /utf-8 /EHsc)
else()
  target_compile_options(ui-model-test PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

add_test(NAME ui-model COMMAND ui-model-test)

add_executable(usb-cards-test test/usb-cards-test.cpp)
find_path(ARDUINOJSON_INCLUDE_DIR ArduinoJson.h HINTS "${CMAKE_SOURCE_DIR}/.pio/libdeps/cyd/ArduinoJson/src")
if(NOT ARDUINOJSON_INCLUDE_DIR)
  message(FATAL_ERROR "Run pio run first, or set ARDUINOJSON_INCLUDE_DIR to ArduinoJson 6.21.5 src")
endif()
target_include_directories(usb-cards-test PRIVATE include ${ARDUINOJSON_INCLUDE_DIR})
target_compile_features(usb-cards-test PRIVATE cxx_std_17)
if(MSVC)
 target_compile_options(usb-cards-test PRIVATE /UNDEBUG)
 target_compile_options(ui-model-test PRIVATE /UNDEBUG)
else()
 target_compile_options(usb-cards-test PRIVATE -UNDEBUG)
 target_compile_options(ui-model-test PRIVATE -UNDEBUG)
endif()
add_test(NAME usb-cards COMMAND usb-cards-test)
