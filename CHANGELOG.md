# Changelog

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project follows [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0] - 2026-10-06

### Changed

- **Every public name changed**, to match the other NimaLTD libraries. Code
  written for version 2 will not compile until it is updated:

  | Was | Is now |
  |---|---|
  | `spif.h`, `spif.c` at the top of the repository | `src/spif.h`, `src/spif.c` |
  | `NimaLTD.I-CUBE-SPIF_conf.h` | `spif_config.h`, and `osal_config.h` from [osal](https://github.com/nimaltd/osal) |
  | `SPIF_HandleTypeDef` | `spif_t` |
  | `SPIF_Init(...)` | `spif_init(...)`, same arguments |
  | `SPIF_ReadAddress(...)`, `SPIF_WriteAddress(...)` | `spif_read(...)`, `spif_write(...)`, same arguments |
  | `SPIF_EraseSector(...)`, `SPIF_EraseBlock(...)`, `SPIF_EraseChip(...)` | `spif_erase_sector(...)`, `spif_erase_block(...)`, `spif_erase_chip(...)`, same arguments |
  | `SPIF_PLATFORM_HAL`, `SPIF_PLATFORM_HAL_DMA` | `SPIF_TRANSFER_POLLING`, `SPIF_TRANSFER_DMA` |
  | `SPIF_RTOS` | `OSAL_RTOS` |
  | `SPIF_RTOS_DISABLE` | `OSAL_RTOS_NONE` |

- **Every function returns `spif_err_t` instead of `bool`**, and success is
  `SPIF_ERR_NONE`, which is 0. So `if (SPIF_ReadAddress(...))` becomes
  `if (spif_read(...) == SPIF_ERR_NONE)`. A plain rename to
  `if (spif_read(...))` still compiles and means the opposite, so check every
  call.
- Reading and writing go by address only. The page, sector and block versions
  are gone: give `SPIF_PAGE_TO_ADDRESS(page) + offset` as the address instead.
  They also cut a call short at the end of their page, sector or block, which
  `spif_read()` and `spif_write()` do not.
- The conversion macros are renamed and go between an address and a page,
  sector, 32 KB or 64 KB block number: `SPIF_ADDRESS_TO_PAGE()`,
  `SPIF_PAGE_TO_ADDRESS()` and the others. The two for sectors take the handle,
  since a sector's size depends on the chip. Unit to unit, such as
  `SPIF_PageToSector()`, goes through the address.
- Every timeout is set once in `spif_config.h`, a value for each step: a
  transfer, a page, a sector, a 32 KB and a 64 KB block, the whole chip for
  each MB, and the wait for the mutex. The values shipped are a W25Q128JV's
  longest times plus half again. The fixed 100 ms and 2 s of version 2 are gone.
- **Writes and erases return at once**, as jobs, and the chip goes on by
  itself. The new `spif_is_busy()` checks once whether the job is done and
  moves it on, sending the next page of a write or the next piece of an erase,
  for a state machine. The new `spif_wait()` waits for the job to end, sleeping
  through osal, and returns how it ended. Version 2 waited inside each call,
  minutes for a chip erase. With DMA a read is a job too. Only the data goes by
  DMA: commands and status reads, a few bytes each, go by polling. With
  polling, no DMA code is compiled in at all.
- A sector is the smallest part the chip erases, `sector_size` bytes: 4 KB on
  nearly every chip, 64 KB or 256 KB on a Spansion FL-S or an old M25P.
- `spif_write()` takes a pointer to `const` data.
- `spif.h` includes `main.h` instead of `spi.h`, so CubeMX no longer has to
  generate a separate `.c` and `.h` file per peripheral.
- The RTOS is reached through [osal](https://github.com/nimaltd/osal), which the installer
  puts in with spif, and it is set once in `osal_config.h` for every library
  that uses osal. With an RTOS, every call has to come from a thread once the
  RTOS runs, `spif_init()` included.
- Installed with [stm32-installer](https://github.com/nimaltd/stm32-installer)
  rather than the STM32CubeMX pack. `spif_config.h` is replaced on every
  update, and the setting between its `USER CODE` lines is kept.
- Licence changed from GPLv2 or commercial to Apache-2.0.

### Added

- Chips that need handling of their own, each done the way its datasheet says:
  - Micron N25Q and MT25Q: busy and refused writes read from the flag status
    register, which also covers every die of a 1 or 2 Gbit chip.
  - Spansion and Cypress S25FL-S, S25FL-P and S25SL: 64 KB or 256 KB sectors,
    read from the extended ID, and a refused write cleared, where it used to
    leave the chip busy for good.
  - Microchip SST26: unlocked at init, since it locks itself at every power up.
    Its 32 KB and 64 KB erases are done a sector at a time, because its own
    64 KB erase clears 8 KB or 32 KB near the ends.
  - Adesto AT25DF and AT25FF, and the ESMT F25L: unlocked at init too.
  - Adesto AT25SF and AT25DF: their size read from their own form of ID.
  - The old ST M25P: 64 KB sectors, 32 KB on the M25P10, 256 KB on the M25P128.
  - Winbond W25Q01 and W25Q02: each die asked in turn whether it is busy.
- Chips up to 2 Gbit, 256 MB. One over 64 MB is erased block by block, since
  the makers differ in how a stacked chip is erased whole.
- Macronix's 1.8 V chips, whose size codes start at `0x32`.
- `spif_erase_block32()`, which erases 32 KB. A chip without one has it done as
  eight 4 KB sectors.
- `spif_unique_id()`, which reads the chip's 64 bit unique ID, each maker's way:
  Winbond's command for Winbond and the makers that copy it, the OTP area for
  Spansion and the AT25DF, and the Security ID for SST26. Macronix and Micron
  have none set at the factory, and are refused.
- Write protection found by `spif_init()`, which returns the new
  `SPIF_ERR_PROTECTED` with the handle still working. Micron and Spansion
  report a refused write or erase, and spif returns it then too.
- `spif_is_busy()`, `spif_wait()` and `SPIF_ERR_BUSY`, for jobs.
- A real mutex per chip with an RTOS: CMSIS-RTOS v1 and v2 for FreeRTOS, and
  ThreadX, through osal.
- Error values that say what went wrong: `SPIF_ERR_INVALID`, `SPIF_ERR_RANGE`,
  `SPIF_ERR_SPI`, `SPIF_ERR_TIMEOUT`, `SPIF_ERR_MUTEX`, `SPIF_ERR_CHIP`,
  `SPIF_ERR_PROTECTED` and `SPIF_ERR_BUSY`.
- `spif_init()` wakes a chip that an earlier run left in deep power down.
- When `spif_init()` does not know the chip, `manufacturer` and `memory_type`
  still hold what it answered, which says which chip it is.
- LittleFS, as an option of the installer. A yes at the first install, or
  `--with littlefs` later, brings [littlefs](https://github.com/nimaltd/littlefs)
  and the port, `spif_lfs.h` and `spif_lfs.c`: `spif_lfs_init()` fills in the
  file system for the chip, a block a sector, with littlefs's caches in its
  handle so mounting needs no heap. With `LFS_THREADSAFE` it gives littlefs a
  lock through osal. Needs stm32-installer 1.8.0 or newer.
- Host unit tests, run against a model of each kind of chip with
  `python test/run_tests.py`, once by polling and once by DMA, and for the
  LittleFS port, the real littlefs on a fake spif, without `LFS_THREADSAFE` and
  with it.
- CMake build, and a `library.yml` for installing with stm32-installer, from
  GitHub or from a downloaded zip.

### Removed

- `SPIF_DEBUG` and the `printf` output it turned on.

### Fixed

- The lock was not a lock. Two threads could both see it free and both take it.
- A read of 64 KB or more was handed to the HAL whole, which counts in 16 bits.
  64 KB became 0 and was refused, and 70000 bytes became 4464, so only the
  first 4464 bytes were read and the call still returned `true`.
- A read past the end of the chip wrapped round to its start, and a read of a
  page or sector given an offset past its end ran on unchecked. Both are now
  refused with `SPIF_ERR_RANGE` before anything is sent.
- Chips of 32 MB and more were driven with the dedicated 4 byte commands, which
  the Winbond W25Q256FV does not have. Every maker but Spansion is now switched
  to 4 byte addresses at init instead, which every such chip supports, with the
  write enable Micron needs first.
- Reads used the plain read command, which many chips take only up to 50 MHz.
  They use the fast read now, which every chip takes at any clock.
- Reading 0 bytes returned failure. It now succeeds and sends nothing.

## [2.3.2]

The last version installed through the STM32CubeMX pack. Tagged as `v2.3.2`.
