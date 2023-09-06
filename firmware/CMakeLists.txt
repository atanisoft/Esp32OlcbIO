# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.20)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

project(Esp32OlcbIO)

###############################################################################
# Enable usage of std::stoi/stol/etc
###############################################################################

idf_build_set_property(COMPILE_DEFINITIONS "-D_GLIBCXX_USE_C99" APPEND)

###############################################################################
# Compress the web content with gzip
###############################################################################

file(ARCHIVE_CREATE OUTPUT "${BUILD_DIR}/index.html.gz"
  PATHS ${CMAKE_CURRENT_SOURCE_DIR}/web/index.html
  FORMAT raw
  COMPRESSION GZip
  VERBOSE)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${BUILD_DIR}/index.html.gz")

file(ARCHIVE_CREATE OUTPUT "${BUILD_DIR}/cash.min.js.gz"
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/web/cash.min.js"
  FORMAT raw
  COMPRESSION GZip
  VERBOSE)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${BUILD_DIR}/cash.min.js.gz")

file(ARCHIVE_CREATE OUTPUT "${BUILD_DIR}/spectre.min.css.gz"
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/web/spectre.min.css"
  FORMAT raw
  COMPRESSION GZip
  VERBOSE)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${BUILD_DIR}/spectre.min.css.gz")

file(ARCHIVE_CREATE OUTPUT "${BUILD_DIR}/cdi.js.gz"
  PATHS "${CMAKE_CURRENT_SOURCE_DIR}/web/cdi.js"
  FORMAT raw
  COMPRESSION GZip
  VERBOSE)
set_property(TARGET ${CMAKE_PROJECT_NAME}.elf APPEND PROPERTY ADDITIONAL_MAKE_CLEAN_FILES
    "${BUILD_DIR}/cdi.js.gz")

###############################################################################
# Add web content to the binary
###############################################################################

target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${BUILD_DIR}/index.html.gz" BINARY)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${BUILD_DIR}/cash.min.js.gz" BINARY)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${BUILD_DIR}/spectre.min.css.gz" BINARY)
target_add_binary_data(${CMAKE_PROJECT_NAME}.elf "${BUILD_DIR}/cdi.js.gz" BINARY)

###############################################################################
# Configuration validations
###############################################################################

if (NOT CONFIG_FREERTOS_HZ EQUAL 1000)
    message(FATAL_ERROR "FreeRTOS tick rate (hz) is required to be 1000.")
endif()

if (NOT CONFIG_PARTITION_TABLE_FILENAME STREQUAL "esp32io-partitions.csv")
    message(FATAL_ERROR "The custom partition table option is not enabled in menuconfig and is required for compilation.")
endif()

if (NOT CONFIG_PARTITION_TABLE_CUSTOM_FILENAME STREQUAL "esp32io-partitions.csv")
    message(FATAL_ERROR "The custom partition table option is not enabled in menuconfig and is required for compilation.")
endif()

if (NOT CONFIG_PARTITION_TABLE_CUSTOM)
    message(FATAL_ERROR "The custom partition table option is not enabled in menuconfig and is required for compilation.")
endif()

if (NOT CONFIG_LWIP_SO_RCVBUF)
    message(FATAL_ERROR "LwIP SO_RCVBUF is a required option in menuconfig.")
endif()
