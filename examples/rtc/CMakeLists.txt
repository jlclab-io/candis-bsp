cmake_minimum_required(VERSION 3.22)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Repository examples share this component. Exported examples vendor it in components/.
if(EXISTS "${CMAKE_CURRENT_LIST_DIR}/../common/candis_example_usb/CMakeLists.txt")
    list(APPEND EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../common/candis_example_usb")
endif()
idf_build_set_property(MINIMAL_BUILD ON)
project(candis_rtc_example)
