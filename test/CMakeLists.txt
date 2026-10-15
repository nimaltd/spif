# Unity is vendored in test/unity. It is third party code, so it is built as its
# own target and is deliberately left out of this project's -Werror.
add_library(unity STATIC unity/unity.c)
target_include_directories(unity PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/unity)

set(SPIF_WARNINGS -Wall -Wextra -Wpedantic -Werror)

# spif reaches the RTOS only through osal, and the fake osal in fake/ lets the
# tests hold, refuse or delay the mutex at will, so there is no build per RTOS:
# those are osal's tests to run. The fake stands for an RTOS unless a build says
# OSAL_RTOS_NONE, where nothing may sleep and the fake HAL moves the clock on a
# microsecond for each question, as a real CPU spends it.

# One build of the suite, with the settings given. Defining SPIF_CONFIG_H, the
# include guard of spif_config.h, skips the shipped file, and the build then
# gives every setting itself. The definitions are PUBLIC so the test file is
# built with the same settings as the driver it tests.
function(spif_suite name)
    add_library(spif_${name} STATIC ${PROJECT_SOURCE_DIR}/src/spif.c)
    target_include_directories(spif_${name} PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${SPIF_CONFIG_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/fake)
    target_compile_definitions(spif_${name} PUBLIC ${ARGN})
    target_compile_features(spif_${name} PUBLIC c_std_11)

    add_executable(test_spif_${name} test_spif.c)
    target_link_libraries(test_spif_${name} PRIVATE spif_${name} unity)

    # The stubs stand in for the CubeMX main.h and the HAL.
    target_include_directories(test_spif_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub)

    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(spif_${name} PRIVATE ${SPIF_WARNINGS})
        target_compile_options(test_spif_${name} PRIVATE ${SPIF_WARNINGS})
    endif()

    add_test(NAME spif_${name} COMMAND test_spif_${name})
endfunction()

# The times spif_config.h ships, and its piece size.
set(SPIF_SHIPPED
    SPIF_POLL_CHUNK=4096U
    SPIF_TIMEOUT_MUTEX_MS=HAL_MAX_DELAY
    SPIF_TIMEOUT_TRANSFER_MS=400U
    SPIF_TIMEOUT_PAGE_MS=5U
    SPIF_TIMEOUT_SECTOR_MS=600U
    SPIF_TIMEOUT_BLOCK32_MS=2400U
    SPIF_TIMEOUT_BLOCK_MS=3000U
    SPIF_TIMEOUT_CHIP_PER_MB_MS=18750U)

# With spif_config.h exactly as it ships: the calls that wait, by polling.
add_executable(test_spif test_spif.c)
target_link_libraries(test_spif PRIVATE spif unity)
target_include_directories(test_spif PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stub)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(spif PRIVATE ${SPIF_WARNINGS})
    target_compile_options(test_spif PRIVATE ${SPIF_WARNINGS})
endif()

add_test(NAME spif_blocking COMMAND test_spif)

# The calls that wait, by DMA.
spif_suite(dma
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_DMA
    ${SPIF_SHIPPED})

# Jobs, by polling and by DMA.
spif_suite(nb
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_NON_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_POLLING
    ${SPIF_SHIPPED})

spif_suite(nb_dma
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_NON_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_DMA
    ${SPIF_SHIPPED})

# HAL_MAX_DELAY for the sector and the chip erase times, which waits for ever
# as it does in the HAL. Only the tests about that run in this build: every
# other one expects the times spif_config.h ships, and a chip that never
# finishes would never let it go. Jobs by polling, so a test can move the tick
# on in the middle of one.
spif_suite(nb_forever
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_NON_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_POLLING
    SPIF_POLL_CHUNK=4096U
    SPIF_TIMEOUT_MUTEX_MS=HAL_MAX_DELAY
    SPIF_TIMEOUT_TRANSFER_MS=400U
    SPIF_TIMEOUT_PAGE_MS=5U
    SPIF_TIMEOUT_SECTOR_MS=HAL_MAX_DELAY
    SPIF_TIMEOUT_BLOCK32_MS=2400U
    SPIF_TIMEOUT_BLOCK_MS=3000U
    SPIF_TIMEOUT_CHIP_PER_MB_MS=HAL_MAX_DELAY)

# No RTOS, for each mode and each transfer.
spif_suite(bare
    OSAL_RTOS=OSAL_RTOS_NONE)

spif_suite(bare_dma
    OSAL_RTOS=OSAL_RTOS_NONE
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_DMA
    ${SPIF_SHIPPED})

spif_suite(bare_nb
    OSAL_RTOS=OSAL_RTOS_NONE
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_NON_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_POLLING
    ${SPIF_SHIPPED})

spif_suite(bare_nb_dma
    OSAL_RTOS=OSAL_RTOS_NONE
    SPIF_CONFIG_H
    SPIF_MODE=SPIF_MODE_NON_BLOCKING
    SPIF_TRANSFER=SPIF_TRANSFER_DMA
    ${SPIF_SHIPPED})

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
    foreach(target test_spif_lfs test_spif_lfs_threadsafe)
        target_compile_options(${target} PRIVATE ${SPIF_WARNINGS})
    endforeach()
endif()
