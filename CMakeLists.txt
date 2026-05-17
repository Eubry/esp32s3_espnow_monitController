# The following five lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/components/DevLib/Common/libraries/Counter"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/DevLib/ESP-IDF/libraries/Utils"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/DevLib/ESP-IDF/libraries/pinManager"
    "${CMAKE_CURRENT_SOURCE_DIR}/components/DevLib/ESP-IDF/libraries/pitchManager"
)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32s3_espnow_monitController)
