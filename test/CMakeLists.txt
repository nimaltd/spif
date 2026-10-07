# Unity is vendored in test/unity. It is third party code, so it is built as its
# own target and is deliberately left out of this project's -Werror.
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)

set(SPIF_WARNINGS -Wall -Wextra -Wpedantic -Werror)

# spif reaches the RTOS only through osal, and the fake osal in fake/ lets the
# tests hold, refuse or delay the mutex at will, so there is no build per RTOS:
# those are osal's tests to run. There is one per SPIF_TRANSFER setting.

# Polling, with spif_config.h exactly as it ships.
add_executable(test_spif test_spif.c)
target_link_libraries(test_spif PRIVATE spif unity)

# DMA. Defining the include guard of spif_config.h skips the shipped file, and
# the settings are given here instead, the timeouts as it ships them. The
# definitions are PUBLIC so the test file is built with the same settings as
# the driver it tests.
add_library(spif_dma STATIC ${PROJECT_SOURCE_DIR}/src/spif.c)
target_include_directories(spif_dma PUBLIC
    ${PROJECT_SOURCE_DIR}/src
    ${SPIF_CONFIG_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/fake)
target_compile_definitions(spif_dma PUBLIC
    SPIF_CONFIG_H
    SPIF_TRANSFER=SPIF_TRANSFER_DMA
    SPIF_TIMEOUT_MUTEX_MS=HAL_MAX_DELAY
    SPIF_TIMEOUT_TRANSFER_MS=400U
    SPIF_TIMEOUT_PAGE_MS=5U
    SPIF_TIMEOUT_SECTOR_MS=600U
    SPIF_TIMEOUT_BLOCK32_MS=2400U
    SPIF_TIMEOUT_BLOCK_MS=3000U
    SPIF_TIMEOUT_CHIP_PER_MB_MS=18750U)
target_compile_features(spif_dma PUBLIC c_std_11)

add_executable(test_spif_dma test_spif.c)
target_link_libraries(test_spif_dma PRIVATE spif_dma unity)

# The stubs stand in for the CubeMX main.h and the HAL.
target_include_directories(test_spif PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub)
target_include_directories(test_spif_dma PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub)

# The LittleFS port, src/spif_lfs.c, over a fake spif the test file provides,
# with the real littlefs on top. littlefs is vendored in test/littlefs at
# v2.11.3, third party code built as its own target and left out of -Werror
# like Unity. Its asserts stay on, so a setting the port gets wrong stops the
# test, and its messages are off. Built twice, without LFS_THREADSAFE and with
# it, which changes struct lfs_config, so the define is PUBLIC and reaches every
# file that includes lfs.h.
foreach(variant lfs lfs_threadsafe)
    add_library(littlefs_${variant} STATIC littlefs/lfs.c littlefs/lfs_util.c)
    target_include_directories(littlefs_${variant} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/littlefs)
    target_compile_definitions(littlefs_${variant} PUBLIC LFS_NO_DEBUG LFS_NO_WARN LFS_NO_ERROR)

    add_executable(test_spif_${variant} test_spif_lfs.c ${PROJECT_SOURCE_DIR}/src/spif_lfs.c)
    target_include_directories(test_spif_${variant} PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/stub
        ${CMAKE_CURRENT_SOURCE_DIR}/fake)
    target_link_libraries(test_spif_${variant} PRIVATE littlefs_${variant} unity)
    target_compile_features(test_spif_${variant} PRIVATE c_std_11)

    add_test(NAME spif_${variant} COMMAND test_spif_${variant})
endforeach()

target_compile_definitions(littlefs_lfs_threadsafe PUBLIC LFS_THREADSAFE)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    foreach(target spif spif_dma test_spif test_spif_dma test_spif_lfs test_spif_lfs_threadsafe)
        target_compile_options(${target} PRIVATE ${SPIF_WARNINGS})
    endforeach()
endif()

add_test(NAME spif_polling COMMAND test_spif)
add_test(NAME spif_dma COMMAND test_spif_dma)
