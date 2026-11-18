cmake_minimum_required(VERSION 3.13)

# Waveshare RP2350-Touch-AMOLED-1.8: RP2350A with 16MB flash. The SDK has no
# board file for it, so use pico2 and override the flash size.
set(PICO_BOARD pico2 CACHE STRING "Board type")
set(PICO_PLATFORM rp2350-arm-s CACHE STRING "Platform")

# Use PICO_SDK_PATH if set (the VS Code Pico extension sets it); otherwise
# fetch the SDK from GitHub at configure time.
if(NOT DEFINED PICO_SDK_PATH AND NOT DEFINED ENV{PICO_SDK_PATH})
  set(PICO_SDK_FETCH_FROM_GIT ON)
endif()
include(pico_sdk_import.cmake)

project(rp2350_eyeballs C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

add_compile_definitions(PICO_FLASH_SIZE_BYTES=16777216)
pico_sdk_init()

add_subdirectory(firmware/board)
add_subdirectory(firmware/bringup)
add_subdirectory(firmware/eye)
add_subdirectory(firmware/common)
add_subdirectory(firmware/eyetest)
add_subdirectory(firmware/pendant)
