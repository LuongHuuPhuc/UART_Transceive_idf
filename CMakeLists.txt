cmake_minimum_required(VERSION 3.5)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(IDF_TARGET esp32s3)

set(EXTRA_COMPONENT_DIRS ${CMAKE_CURRENT_SOURCE_DIR}../../Wireless_network_test/components
                         ${CMAKE_CURRENT_SOURCE_DIR}../components)

project(uart_transceive)