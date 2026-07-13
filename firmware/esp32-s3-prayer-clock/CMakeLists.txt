cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
if(DEFINED ENV{ADHAN_FIRMWARE_VERSION})
    set(PROJECT_VER "$ENV{ADHAN_FIRMWARE_VERSION}")
endif()
project(adhancron_prayer_clock)
