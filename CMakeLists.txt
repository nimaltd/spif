cmake_minimum_required(VERSION 3.16)

project(spif
    VERSION     3.0.0
    LANGUAGES   C
    DESCRIPTION "Driver for SPI NOR flash, W25Qxx and compatible, on the STM32 HAL"
    HOMEPAGE_URL "https://github.com/nimaltd/spif")

option(SPIF_BUILD_TESTS "Build the host unit tests" OFF)
option(SPIF_LITTLEFS "Build the LittleFS port, src/spif_lfs.c, with nimaltd::littlefs" OFF)

# spif.h includes the platform main.h, which it does not ship, for the HAL's
# SPI and GPIO types. Point SPIF_CONFIG_DIR at the folder holding it, normally
# Core/Inc in a CubeMX project. The tests supply a stub instead.
if(SPIF_BUILD_TESTS AND NOT DEFINED SPIF_CONFIG_DIR)
    set(SPIF_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/test/stub")
endif()

add_library(spif STATIC src/spif.c)
add_library(nimaltd::spif ALIAS spif)

target_include_directories(spif PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

target_compile_features(spif PUBLIC c_std_11)

if(DEFINED SPIF_CONFIG_DIR)
    target_include_directories(spif PUBLIC ${SPIF_CONFIG_DIR})
endif()

# spif waits and locks through osal. Add nimaltd/osal to the build as well, with
# add_subdirectory(osal), before or after this one. The tests use a fake osal
# instead, which lets them hold or refuse the mutex.
if(SPIF_BUILD_TESTS)
    target_include_directories(spif PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/test/fake)
else()
    target_link_libraries(spif PUBLIC nimaltd::osal)
endif()

# The LittleFS port, what the installer's littlefs option brings. Add
# nimaltd/littlefs to the build as well, with add_subdirectory(littlefs): it
# brings lfs.h and the LFS_DEFINES every file using it is built with.
if(SPIF_LITTLEFS)
    target_sources(spif PRIVATE src/spif_lfs.c)
    target_link_libraries(spif PUBLIC nimaltd::littlefs)
endif()

if(SPIF_BUILD_TESTS)
    enable_testing()
    add_subdirectory(test)
endif()
