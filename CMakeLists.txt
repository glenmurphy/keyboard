set(PROJECTNAME keyboard)

cmake_minimum_required(VERSION 3.14)
include(pico_sdk_import.cmake)

project(${PROJECTNAME} VERSION 1.0.0)
add_executable(${PROJECTNAME}
               main.c
               usb_descriptors.c
               keyboard.c)

pico_sdk_init()

#pico_enable_stdio_usb(test2 1)
#pico_enable_stdio_uart(test2 1)
#pico_add_extra_outputs(test2)

target_link_libraries(${PROJECTNAME}
                      pico_stdlib
                      tinyusb_device
                      tinyusb_board
                      hardware_gpio
                      hardware_adc
                      #hardware_i2c
                      #hardware_spi
                      hardware_uart)

#TinyUSB stuff so it can pick up tinyusb_config.h
target_include_directories(${PROJECTNAME} PRIVATE 
                           ${CMAKE_CURRENT_LIST_DIR})

pico_add_extra_outputs(${PROJECTNAME})