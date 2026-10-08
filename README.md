# 💽 spif

[![CI](https://github.com/nimaltd/spif/actions/workflows/ci.yml/badge.svg)](https://github.com/nimaltd/spif/actions/workflows/ci.yml)
[![Stars](https://img.shields.io/github/stars/nimaltd/spif?style=social)](https://github.com/nimaltd/spif)
[![License](https://img.shields.io/badge/license-Apache--2.0-blue)](LICENSE.md)

A driver for SPI NOR flash, W25Qxx and the many compatible chips from every well known maker, written in C for the STM32 HAL.

Read or write any number of bytes at any address, and erase by sector, block or the whole chip. The library finds the chip and its size by itself, and deals with pages, 4 byte addresses, each maker's own ways, the chip's write and erase times, and the limits of the HAL. It works on any STM32 family, bare metal or with FreeRTOS or ThreadX, by polling or by DMA.

---

## ✨ What you get

- Any length at any address. Page boundaries and the 64 KB HAL limit are
  handled for you
- The chip and its size found from its JEDEC ID, 128 KB to 256 MB, so one build
  works with whatever chip is fitted
- Winbond, GigaDevice, Macronix, Micron, ISSI, Spansion and Cypress, Adesto
  AT25SF and AT25DF, Microchip SST26, XMC, Puya, Zbit, Boya and more, each
  handled the way its datasheet says
- Erase a sector, a 32 KB or 64 KB block, or the whole chip. A chip without one
  of those sizes has it done in smaller pieces, or refused, never erased wrong
- Writes and erases that return at once, so a state machine can do other work
  and ask `spif_is_busy()` later, or call `spif_wait()` to wait for them
- Polling or DMA for the data, chosen once in `spif_config.h`
- Write protection found at start up, and SST26 and AT25DF, which lock
  themselves at power up, unlocked
- The chip's 64 bit unique ID, read each maker's way, for a serial number or a key
- Every timeout set once in `spif_config.h`, from the datasheet plus half
- Writes and erases wait only as long as the chip needs, by asking it every
  millisecond, and with an RTOS your other threads run meanwhile
- FreeRTOS, through CMSIS-RTOS v1 or v2, and ThreadX, by way of
  [osal](https://github.com/nimaltd/osal): a mutex per chip
- A read or write past the end of the chip is refused, never wrapped round to
  overwrite the start
- [LittleFS](#a-file-system-littlefs) ready to use: answer yes at install, and
  the port and littlefs come with it
- Unit tested on every commit, against a model of each kind of chip, both by
  polling and by DMA

---

## 🔌 Supported chips

| Maker | Chips | What spif does for them |
|---|---|---|
| Winbond | W25Q, W25X, 1 Mbit to 2 Gbit | The reference. A 1 or 2 Gbit chip is two or four dies, each asked in turn whether it is busy |
| GigaDevice, XMC, Puya, Zbit, Boya, ISSI, EON, ESMT, Fudan | GD25Q, XM25QH, P25Q, ZB25VQ, BY25Q, IS25LP and IS25WP, EN25Q, F25L and others | Work like a Winbond. The ESMT F25L locks itself at power up and is unlocked |
| Macronix | MX25L, MX25R, MX25U, MX66L, up to 2 Gbit | Work like a Winbond, 1.8 V parts included |
| Micron and ST | N25Q, MT25Q up to 2 Gbit, and the old M25P | Busy and refused writes read from the flag status register. The M25P has 64 KB sectors, 32 KB on the M25P10 and 256 KB on the M25P128 |
| Spansion, Cypress, Infineon | S25FL-S, S25FL-P, S25FL-L, S25FL1-K | The FL-S and FL-P have 64 KB or 256 KB sectors, read from their ID, and a refused write is cleared so the chip does not stay busy |
| Adesto, Renesas | AT25SF, AT25SL, AT25DF, AT25FF | Their size read from their own ID. The AT25DF locks itself at power up and is unlocked |
| Microchip | SST26VF, SST26WF | Locked at every power up and unlocked. 32 KB and 64 KB erases are done a sector at a time, since its 64 KB erase clears less near the ends |

Up to 16 MB a chip takes 3 address bytes. Above that `spif_init()` switches it to 4, and Spansion, which has no such switch, gets its own 4 byte commands instead.

Not supported, and refused by `spif_init()` with `SPIF_ERR_CHIP`:

- SST25, which programs a byte or a word at a time rather than a page
- Adesto AT45DB DataFlash, which works in a different way altogether
- Infineon Semper S25HL and S25HS, whose sectors and errors need handling of their own
- NAND flash such as the W25N, which is a different kind of chip

---

## 📁 Layout

```
src/    spif.h, spif.c, spif_config.h
        spif_lfs.h, spif_lfs.c     the LittleFS port, installed when asked for
test/   host unit tests, run on a PC
```

Installed into a project, the code keeps its `src/` folder, with the README,
changelog and licence files around it. There is no `test/`: the section below
about the tests refers to this repository, not to an installed copy.

---

## ⚙️ Installing it

[stm32-installer](https://github.com/nimaltd/stm32-installer) copies the library into your project and adds it to your CMake, STM32CubeIDE, Keil, IAR or Makefile project for you. Your project file is backed up first. It also checks that SPI is enabled in your CubeMX project, and says so if it is not.

Install it once per machine:

```bash
pip install stm32-installer
```

Then, from the root of your STM32 project:

```bash
stm32-installer nimaltd/spif
```

spif waits and locks through [osal](https://github.com/nimaltd/osal), so the installer puts osal in first, and asks for its folder like any library. If another library already brought osal into your project, that copy is kept and nothing is asked.

The first install also asks whether you want the [LittleFS file system](#a-file-system-littlefs). The answer is kept, so an update does not ask again.

### From a downloaded zip

Downloaded this repository with **Code**, **Download ZIP**? Give the installer the zip in place of `nimaltd/spif`, with no need to unpack it:

```bash
stm32-installer D:/Downloads/spif-main.zip
```

Only the files the library needs are copied into your project, and the zip is left alone. An unpacked folder works the same way. On a machine with no internet, give it the osal zip as well, `stm32-installer D:/Downloads/spif-main.zip D:/Downloads/osal-main.zip`, so it does not try to fetch osal. [stm32-installer's README](https://github.com/nimaltd/stm32-installer#installing-a-library) has every option, and how to install on a machine with no internet at all.

### Updating, and pinning a version

Run the same command again. The code is replaced. osal is updated only when spif needs a newer one. Your settings in `spif_config.h` and `osal_config.h`, between their `USER CODE` lines, are kept either way. This needs stm32-installer 1.9.0 or newer. An older one cannot find the library: update it with `pip install --upgrade stm32-installer`.

By default you get the newest code on `main`. To hold a project on one release, add `--ref` with a tag, a branch or a commit:

```bash
stm32-installer nimaltd/spif --ref v3.0.0
```

### Or copy the files in by hand

1. Copy `src/spif.h` and `src/spif_config.h` into your project's `Core/Inc`
2. Copy `src/spif.c` into your project's `Core/Src`
3. Copy every header in the `src` folder of [osal](https://github.com/nimaltd/osal) into `Core/Inc`
4. For LittleFS, also copy `src/spif_lfs.h` and `src/spif_lfs.c`, and the `lfs*.h` and `lfs*.c` files of [littlefs](https://github.com/nimaltd/littlefs), and add `LFS_DEFINES=lfs_defines.h` to your project's defines, beside `USE_HAL_DRIVER`

### Or add the whole repository to a CMake build

If you keep this repository as a submodule rather than installing it:

```cmake
add_subdirectory(osal)     # spif needs it, and links it for you
add_subdirectory(spif)
target_link_libraries(${CMAKE_PROJECT_NAME} nimaltd::spif)

# spif is a static library, so it does not inherit your application's include
# paths and defines, and spif.h needs main.h and the HAL. A CubeMX project
# keeps them on the stm32cubemx target.
target_link_libraries(spif PRIVATE stm32cubemx)
```

The first `target_link_libraries` has no `PRIVATE` on purpose. CubeMX links your application without one, and CMake refuses to mix the two forms on one target. The settings come from `spif/src/spif_config.h` and `osal/src/osal_config.h`.

For LittleFS, add [littlefs](https://github.com/nimaltd/littlefs) the same way, with `add_subdirectory(littlefs)`, and set `SPIF_LITTLEFS` on before spif's line, with `set(SPIF_LITTLEFS ON)`, to build the port into spif.

`stm32-installer` avoids all of this: it writes an INTERFACE target instead, whose sources compile as part of your own target and inherit everything it has.

---

## 🔧 Configuration

spif's settings are in `spif_config.h`, between its `USER CODE` lines.

### Polling or DMA

```c
#define SPIF_TRANSFER                   SPIF_TRANSFER_POLLING
```

| Value | A read or write |
|---|---|
| `SPIF_TRANSFER_POLLING` | Moves its data with the CPU. A read returns with the data. The default |
| `SPIF_TRANSFER_DMA` | Moves its data by DMA, and a read becomes a job like a write: it returns once started |

Commands and status reads always go by polling: a few bytes take less time than a DMA takes to start. Only the code for the setting you pick is compiled in.

### Timeouts

Each step has its own time, in milliseconds, after which it ends with `SPIF_ERR_TIMEOUT`. `HAL_MAX_DELAY` in any of them waits for ever, as it does in the HAL. The values shipped are the longest times in a Winbond W25Q128JV's datasheet, plus half again:

| Setting | For | Shipped |
|---|---|---|
| `SPIF_TIMEOUT_MUTEX_MS` | Waiting for another thread to finish with the chip | `HAL_MAX_DELAY` |
| `SPIF_TIMEOUT_TRANSFER_MS` | One SPI transfer of up to 32 KB | 400 |
| `SPIF_TIMEOUT_PAGE_MS` | Storing one 256 byte page, 3 ms at most | 5 |
| `SPIF_TIMEOUT_SECTOR_MS` | Erasing one sector, 400 ms at most for 4 KB | 600 |
| `SPIF_TIMEOUT_BLOCK32_MS` | Erasing 32 KB, 1.6 s at most | 2400 |
| `SPIF_TIMEOUT_BLOCK_MS` | Erasing 64 KB, 2 s at most | 3000 |
| `SPIF_TIMEOUT_CHIP_PER_MB_MS` | Erasing the whole chip, for each MB it holds: 200 s at most for 16 MB | 18750 |

Check your own chip's datasheet and fit them to it. An older chip can take longer, and a chip whose sectors are 64 KB or 256 KB takes as long for one as for a block. A write gets the page time for each page, so 4 KB, sixteen pages, can take up to 80 ms. Waiting for the mutex for ever is safe: whoever has the chip gives it back within the other times.

### The RTOS

The RTOS setting lives in your `osal_config.h`, where every NimaLTD library that uses osal reads it:

```c
#define OSAL_RTOS           OSAL_RTOS_NONE
```

| Value | For |
|---|---|
| `OSAL_RTOS_NONE` | Bare metal, no RTOS |
| `OSAL_RTOS_CMSIS_V1` | FreeRTOS through CMSIS-RTOS v1, `cmsis_os.h` |
| `OSAL_RTOS_CMSIS_V2` | FreeRTOS through CMSIS-RTOS v2, `cmsis_os2.h` |
| `OSAL_RTOS_THREADX` | ThreadX, `tx_api.h` |

In CubeMX, FreeRTOS asks which CMSIS-RTOS interface to use when you enable it. Pick the same one here. What changes with an RTOS is described [below](#with-an-rtos).

---

## 🚀 Getting started

In CubeMX:

1. Set the SPI to **Full-Duplex Master**, 8 bits, MSB first, CPOL Low and CPHA 1 Edge (mode 0). Any clock up to the chip's limit works, and every chip above takes 50 MHz or more
2. Set a free pin as **GPIO_Output**, output level **High**, and name it `FLASH_CS`. That is the chip select
3. On the board, tie the chip's WP and HOLD pins to its supply. A HOLD pin left floating can pause the chip at random

Then:

```c
#include "spif.h"

spif_t flash;

int main(void)
{
    /* ... HAL init, MX_GPIO_Init(), MX_SPI1_Init() ... */

    if (spif_init(&flash, &hspi1, FLASH_CS_GPIO_Port, FLASH_CS_Pin) == SPIF_ERR_NONE)
    {
        uint8_t data[64] = "Hello, flash";

        spif_erase_sector(&flash, 0);
        spif_wait(&flash);
        spif_write(&flash, 0, data, sizeof(data));
        spif_wait(&flash);
        spif_read(&flash, 0, data, sizeof(data));
        spif_wait(&flash);
    }

    while (1)
    {
    }
}
```

`flash.size` then holds the size of the chip in bytes, `flash.sector_size` the size of a sector, and `flash.manufacturer` its maker, one of the `SPIF_MANUFACTURER_` values.

A write or an erase only starts the job and returns at once, so `spif_wait()` waits for it, as [below](#jobs-spif_is_busy-and-spif_wait). It returns how the job ended: `SPIF_ERR_NONE` when it worked, which is 0. So check for it by name: `if (spif_wait(...))` would mean "if it failed".

### Erase before you write

A write can only turn bits from 1 to 0. Turning them back to 1 takes an erase, and the smallest erase clears a whole sector, to `0xFF`. So writing over data that was not erased gives the two ANDed together: `0xF0` written over `0x0F` reads back `0x00`, with no error, because the chip did exactly what it was told.

The erase functions take a number, not an address, and macros turn one into the other. On a chip with 4 KB sectors:

```c
spif_erase_sector(&flash, SPIF_ADDRESS_TO_SECTOR(&flash, 0x12345));  /* sector 0x12, 0x12000 to 0x12FFF */
spif_wait(&flash);
spif_erase_block(&flash, SPIF_ADDRESS_TO_BLOCK(0x12345));            /* block 1, 0x10000 to 0x1FFFF   */
spif_wait(&flash);
spif_write(&flash, SPIF_SECTOR_TO_ADDRESS(&flash, 0x12), data, 16);  /* at 0x12000                    */
spif_wait(&flash);
```

A sector is 4 KB on nearly every chip, and 64 KB or 256 KB on a Spansion FL-S or an old M25P, so the two sector macros take the handle and use its `sector_size`. On a chip with 256 KB sectors the same address is in sector 0.

To change a few bytes and keep the rest of a sector, read the sector into RAM, change it there, erase the sector and write it back. That needs a sector of RAM and wears the sector a little each time, which is why a file system is the usual answer.

### Jobs: spif_is_busy() and spif_wait()

A write or an erase is a job: the call starts it and returns at once, and the chip goes on by itself. Erasing a 16 MB chip can take minutes, a sector tens of milliseconds, so the CPU is yours meanwhile. With DMA a read is a job too. Two calls follow a job:

- `spif_is_busy()` checks once and returns at once: `true` while the job runs. A write goes a page at a time, and an erase in pieces on some chips, so each call also sends the next page or piece once the chip has finished the one before. Call it from a state machine or the main loop.
- `spif_wait()` waits until the job has ended, sleeping a millisecond at a time through osal so that with an RTOS other threads run, and returns how it ended. Called once `spif_is_busy()` has said `false`, it returns that at once.

```c
/* In a state machine: start the erase, and come back to it. */
case STATE_ERASE:
    spif_erase_chip(&flash);
    state = STATE_ERASING;
    break;

case STATE_ERASING:
    if (!spif_is_busy(&flash))
    {
        state = (spif_wait(&flash) == SPIF_ERR_NONE) ? STATE_WRITE : STATE_FAILED;
    }
    break;
```

```c
/* Or simply, one step after the other. */
spif_erase_sector(&flash, 3);

if (spif_wait(&flash) == SPIF_ERR_NONE)
{
    spif_write(&flash, SPIF_SECTOR_TO_ADDRESS(&flash, 3), data, sizeof(data));
    err = spif_wait(&flash);
}
```

`spif_wait()` also reports a call that was refused at once, past the end of the chip for one, so simple code needs only it. Until a job has ended, its buffer must stay where it is and unchanged, and any other spif call on that chip returns `SPIF_ERR_BUSY`. Each step of a job keeps to its own time from `spif_config.h`, and a step that takes longer ends the job with `SPIF_ERR_TIMEOUT`.

### DMA

By polling the CPU moves the data. Take an SPI clock of 21 MHz: 4 KB takes about 1.6 ms on the wire, and by polling the CPU sits in the HAL for those 1.6 ms. By DMA it is free for them, and a read returns at once, as a job. A write by DMA still comes back for each page, every half a millisecond or so, so it pays off only if `spif_is_busy()` is called often. DMA pays off most for reads of a few KB at a time.

For DMA, set up CubeMX like this:

1. In the SPI's **DMA Settings**, add a stream for `SPIx_RX` and one for `SPIx_TX`, both Normal mode, byte wide. A read needs both: the HAL clocks bytes out while it receives
2. In **NVIC Settings**, enable the SPI global interrupt. CubeMX enables the DMA stream interrupts for you
3. Set `SPIF_TRANSFER` to `SPIF_TRANSFER_DMA` in `spif_config.h`

On an F7 or H7 with the data cache on, the DMA and the CPU can see different copies of a buffer. Keep the buffers in memory the cache does not cover, or turn the cache off. On an H7, DMA1 and DMA2 cannot reach the DTCM RAM at `0x20000000`, so keep the buffers in AXI SRAM at `0x24000000` or higher.

### A file system, LittleFS

[LittleFS](https://github.com/littlefs-project/littlefs) is made for exactly this kind of chip: it survives a power cut part way through a write, and spreads the wear. spif comes with the port for it, as an option of the installer, which asks at the first install:

```
Options
  littlefs   LittleFS file system on the flash. Add it? [y/N]: y
```

A yes installs [littlefs](https://github.com/nimaltd/littlefs) in a folder of its own, and the port, `spif_lfs.h` and `spif_lfs.c`, beside spif. Later, `stm32-installer nimaltd/spif --with littlefs` adds it, and `--without littlefs` takes the port out again.

After `spif_init()`, the port fills in everything littlefs needs to know about the chip:

```c
#include "spif_lfs.h"

extern spif_t flash;

static spif_lfs_t fs;
lfs_t             lfs;

bool storage_start(void)
{
    int err = LFS_ERR_IO;

    if (spif_lfs_init(&fs, &flash) == SPIF_ERR_NONE)
    {
        err = lfs_mount(&lfs, &fs.cfg);

        /* A new chip, or one that never held a file system, mounts as corrupt.
           That, and only that, is the time to format. After a read that failed,
           formatting would wipe every file to make up for a loose wire. */
        if (err == LFS_ERR_CORRUPT)
        {
            err = lfs_format(&lfs, &fs.cfg);

            if (err == LFS_ERR_OK)
            {
                err = lfs_mount(&lfs, &fs.cfg);
            }
        }
    }

    return (err == LFS_ERR_OK);
}
```

Files then work the way you would expect. This one counts how many times the board has started:

```c
uint32_t   boots = 0;
lfs_file_t file;

lfs_file_open(&lfs, &file, "boots", LFS_O_RDWR | LFS_O_CREAT);
lfs_file_read(&lfs, &file, &boots, sizeof(boots));
boots++;
lfs_file_rewind(&lfs, &file);
lfs_file_write(&lfs, &file, &boots, sizeof(boots));
lfs_file_close(&lfs, &file);
```

The file is stored when `lfs_file_close()` returns. Cut the power before that, and the old count is still there, never a half written one.

The whole chip is the file system, one block a sector. Each step waits until the chip has done it, by polling or by DMA alike. A step spif refuses, because a job you started is still running, fails rather than being skipped, so finish your own jobs with `spif_wait()` before using the file system.

**What it costs.** littlefs is 13.8 KB of flash on a Cortex-M4 at `-Os`, with its messages and asserts off as `lfs_defines.h` ships it, and the port 283 bytes. In RAM, `spif_lfs_t` is 616 bytes, most of it littlefs's two caches, `lfs_t` 128, and each open file 84 plus a 256 byte cache, which `lfs_file_open()` takes from the heap. Give it one with `lfs_file_opencfg()` instead to keep littlefs off the heap.

**Settings.** Two can be set in `spif_config.h`, between its `USER CODE` lines, and both are optional:

| Setting | Default | What it is |
|---|---|---|
| `SPIF_LFS_CACHE_SIZE` | 256 | Bytes in each of littlefs's two caches, held in `spif_lfs_t`. A power of two from 16 to the chip's sector size. Bigger means fewer transfers |
| `SPIF_LFS_LOOKAHEAD_SIZE` | 16 | Bytes of littlefs's map of free blocks, one bit a sector, so 16 bytes cover 512 KB. A multiple of 8 |

Anything else littlefs takes, such as `block_cycles`, can be changed in `fs.cfg` after `spif_lfs_init()` and before `lfs_mount()`. littlefs's own switches, such as its messages, are in `lfs_defines.h` in its folder.

**With more than one thread.** spif's mutex protects one access to the chip, and a file operation makes many. Add `#define LFS_THREADSAFE` to `lfs_defines.h`, between its `USER CODE` lines, and the port gives littlefs a lock of its own, through osal, held for as long as `SPIF_TIMEOUT_MUTEX_MS` allows. Without it, use the file system from one thread.

With DMA on an F7 or H7, littlefs's caches inside `spif_lfs_t` are what the DMA reads into, so the note on the data cache above applies to `fs` as well.

littlefs has its own BSD-3-Clause license, and its `LICENSE.md` is installed with it. A product that ships it has to reproduce that notice in its documentation.

### Write protection

`spif_init()` reads the chip's protection bits, BP0 to BP2, which every maker keeps in the same place. If any is set it returns `SPIF_ERR_PROTECTED`. The handle still works: reads do, and so do writes outside the protected part. But most chips ignore a write or an erase in the protected part without a word, so the chip has to be unprotected first, by whatever set those bits.

An SST26 or an AT25DF locks itself at every power up, by design, and `spif_init()` unlocks it. `SPIF_ERR_PROTECTED` from one of those means a lock that cannot be lifted: an SST26 block locked for ever, or an AT25DF whose WP pin is held low.

A Micron or a Spansion FL-S reports a refused write or erase, and spif returns `SPIF_ERR_PROTECTED` for it then too.

### With an RTOS

Set `OSAL_RTOS` in your `osal_config.h` and each chip gets its own mutex, created by `spif_init()`. Two threads can then use one chip safely: the second waits for the first. While a write or an erase waits for the chip, the thread sleeps and your other threads run.

Start the RTOS first. With one set, call every spif function from a thread, `spif_init()` included, and never from `main()` before the kernel starts: the mutex and the sleeps need a running RTOS.

```c
void StartDefaultTask(void *argument)
{
    spif_init(&flash, &hspi1, FLASH_CS_GPIO_Port, FLASH_CS_Pin);
    storage_start();

    for (;;)
    {
        /* ... */
    }
}
```

A job holds the mutex from the call that starts it until `spif_is_busy()` or `spif_wait()` sees it end, so call those from the thread that started it. `spif_wait()` sleeps between its questions; a loop of your own around `spif_is_busy()` should too, `osDelay(1)` for one, rather than spin.

The mutex is per chip, not per bus. Two devices on one SPI bus used from two threads at once would collide. Usually the HAL refuses the second transfer, which comes back as `SPIF_ERR_SPI`, but that is luck rather than safety. Use both from one thread, or guard the bus with a mutex of your own.

### What is safe to call from an interrupt

Nothing. A write or an erase waits for the chip for milliseconds or seconds, and with an RTOS every call takes a mutex. Hand the work to a task or to the main loop instead, for example with [seq](https://github.com/nimaltd/seq).

A common trap is USB mass storage: CubeMX's USB device library calls `STORAGE_Read_FS()` and `STORAGE_Write_FS()` in `usbd_storage_if.c` from the USB interrupt, so spif cannot be called there.

---

## 🧰 API

| Function | What it does |
|---|---|
| `spif_err_t spif_init(spif_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port, uint16_t cs_pin)` | Set up a handle for one chip, find the chip and read its size. Call it once per handle |
| `spif_err_t spif_read(spif_t *handle, uint32_t address, uint8_t *data, size_t len)` | Read `len` bytes from `address`. With DMA, start reading them, as a job |
| `spif_err_t spif_write(spif_t *handle, uint32_t address, const uint8_t *data, size_t len)` | Start writing `len` bytes to `address`, erased beforehand, as a job |
| `spif_err_t spif_erase_sector(spif_t *handle, uint32_t sector)` | Start erasing sector number `sector`, `sector_size` bytes, as a job |
| `spif_err_t spif_erase_block32(spif_t *handle, uint32_t block32)` | Start erasing 32 KB block number `block32`, as a job |
| `spif_err_t spif_erase_block(spif_t *handle, uint32_t block)` | Start erasing 64 KB block number `block`, as a job |
| `spif_err_t spif_erase_chip(spif_t *handle)` | Start erasing the whole chip, as a job |
| `spif_err_t spif_unique_id(spif_t *handle, uint8_t *id)` | Read the chip's 64 bit unique ID into `SPIF_UNIQUE_ID_SIZE` bytes |
| `bool spif_is_busy(spif_t *handle)` | Whether a job still runs. Checks once, moves it on, and returns at once |
| `spif_err_t spif_wait(spif_t *handle)` | Wait until the job has ended, and return how it ended |

With the littlefs option, `spif_lfs.h` adds one more:

| Function | What it does |
|---|---|
| `spif_err_t spif_lfs_init(spif_lfs_t *handle, spif_t *flash)` | Fill in a file system for a chip `spif_init()` accepted. Then `lfs_mount(&lfs, &handle->cfg)`. `SPIF_ERR_INVALID` for a chip `spif_init()` refused, `SPIF_ERR_MUTEX` when, with `LFS_THREADSAFE`, the RTOS could not create the mutex |

| Macro | Gives |
|---|---|
| `SPIF_ADDRESS_TO_PAGE(address)` | The number of the 256 byte page `address` is in |
| `SPIF_ADDRESS_TO_SECTOR(handle, address)` | The number of the sector `address` is in, for `spif_erase_sector()` |
| `SPIF_ADDRESS_TO_BLOCK32(address)` | The number of the 32 KB block `address` is in, for `spif_erase_block32()` |
| `SPIF_ADDRESS_TO_BLOCK(address)` | The number of the 64 KB block `address` is in, for `spif_erase_block()` |
| `SPIF_PAGE_TO_ADDRESS(page)` | The address `page` starts at |
| `SPIF_SECTOR_TO_ADDRESS(handle, sector)` | The address `sector` starts at |
| `SPIF_BLOCK32_TO_ADDRESS(block32)` | The address `block32` starts at |
| `SPIF_BLOCK_TO_ADDRESS(block)` | The address `block` starts at |

To go from one unit to another, go through the address: the first page of sector 5 is `SPIF_ADDRESS_TO_PAGE(SPIF_SECTOR_TO_ADDRESS(&flash, 5))`.

| Error | Means |
|---|---|
| `SPIF_ERR_NONE` | Done, or for a job, started |
| `SPIF_ERR_INVALID` | A handle `spif_init()` did not accept, an erase smaller than the chip's sector, or a unique ID on a chip without one |
| `SPIF_ERR_RANGE` | `address + len` runs past the end of the chip, or a sector or block number past the last. Nothing was sent |
| `SPIF_ERR_SPI` | The SPI transfer failed. With DMA, also a DMA error |
| `SPIF_ERR_TIMEOUT` | A step took longer than `spif_config.h` allows, or another thread kept the chip too long |
| `SPIF_ERR_MUTEX` | The RTOS could not create the mutex, usually a heap that is too small, or refused it, as from an interrupt |
| `SPIF_ERR_CHIP` | No chip answered, or one this driver does not support |
| `SPIF_ERR_PROTECTED` | Write protection is on, or the chip refused a write or erase. From `spif_init()` the handle still works |
| `SPIF_ERR_BUSY` | A job is still running, so this call was refused. Nothing was sent |

When `spif_init()` fails with anything but `SPIF_ERR_PROTECTED`, the handle is refused by every other call until a later `spif_init()` succeeds.

`spif_unique_id()` knows Winbond, GigaDevice, ISSI, XMC, Puya, Zbit, Boya, Adesto AT25SF and AT25DF, Spansion and SST26. Macronix and Micron have no unique ID set at the factory, and any other maker is not known, so for those it returns `SPIF_ERR_INVALID`.

A `NULL` pointer is a bug in the calling code, so it is not returned as an error: `assert_param()` stops at it, the way the HAL does. That needs **Enable Full Assert** in CubeMX (Project Manager, Code Generator), which defines `USE_FULL_ASSERT`. Turn it on while developing, and a `NULL` lands in `assert_failed()` with the file and line. Without it, nothing checks.

---

## 🧪 Running the tests

The tests run on your PC, not on hardware. The HAL calls land on a model of an SPI NOR flash that behaves as the datasheets describe: it programs only after a write enable and only clears bits, a write wraps inside its page, a busy chip takes nothing but a status read, and a chip over 16 MB takes 4 byte addresses the way its maker does. The model can be a Winbond, a Micron, a Spansion FL-S, an old M25P, an SST26, an AT25DF or a stacked 1 Gbit chip, each with its own registers, sector sizes, locks and unique ID. Time is faked, so an erase of minutes is tested instantly. You need cmake and any C compiler, nothing else: [Unity](https://github.com/ThrowTheSwitch/Unity) is vendored into `test/unity/`, so there is nothing to install.

One command does everything:

```bash
python test/run_tests.py
```

It configures, builds and runs the suite twice, once by polling and once by DMA, then tells you plainly whether it passed. The LittleFS port has a suite of its own, built without `LFS_THREADSAFE` and with it: the real littlefs runs on a fake spif that finishes each job only when `spif_wait()` is called, so a port that skipped a wait would find nothing read and nothing written. littlefs is vendored into `test/littlefs/` for it, at v2.11.3. osal is replaced by a fake the tests control, so a mutex held by another thread, or refused by the RTOS, is tested with no RTOS at all. The RTOS side itself is osal's, and osal's own tests cover it. Add `--clean` to start from an empty build folder.

If you prefer doing it by hand:

```bash
cmake -S . -B build -DSPIF_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## ⬆️ Coming from version 2

The CubeMX pack is replaced by stm32-installer, every name changed, reading and writing go by address only, and the timeouts moved into `spif_config.h`:

| Was | Is now |
|---|---|
| `SPIF_HandleTypeDef` | `spif_t` |
| `SPIF_Init(&h, &hspi1, gpio, pin)` | `spif_init(&h, &hspi1, gpio, pin)` |
| `SPIF_ReadAddress(&h, address, data, size)` | `spif_read(&h, address, data, size)` |
| `SPIF_WriteAddress(&h, address, data, size)` | `spif_write(&h, address, data, size)`, then `spif_wait(&h)` |
| `SPIF_ReadPage(&h, page, data, size, offset)` | `spif_read(&h, SPIF_PAGE_TO_ADDRESS(page) + offset, data, size)` |
| `SPIF_WritePage(&h, page, data, size, offset)` | `spif_write(&h, SPIF_PAGE_TO_ADDRESS(page) + offset, data, size)` |
| `SPIF_ReadSector`, `SPIF_WriteSector` | the same with `SPIF_SECTOR_TO_ADDRESS(&h, sector)` |
| `SPIF_ReadBlock`, `SPIF_WriteBlock` | the same with `SPIF_BLOCK_TO_ADDRESS(block)` |
| `SPIF_EraseSector(&h, sector)` | `spif_erase_sector(&h, sector)`, then `spif_wait(&h)` |
| `SPIF_EraseBlock(&h, block)` | `spif_erase_block(&h, block)`, then `spif_wait(&h)` |
| `SPIF_EraseChip(&h)` | `spif_erase_chip(&h)`, then `spif_wait(&h)` |
| `SPIF_PageToAddress()`, `SPIF_AddressToSector()` and the others | `SPIF_PAGE_TO_ADDRESS()`, `SPIF_ADDRESS_TO_SECTOR(&h, ...)` and the others, [listed above](#-api). Unit to unit goes through the address |
| `NimaLTD.I-CUBE-SPIF_conf.h` | `spif_config.h`, and `osal_config.h` from [osal](https://github.com/nimaltd/osal) |
| `SPIF_PLATFORM_HAL` | `SPIF_TRANSFER_POLLING` |
| `SPIF_PLATFORM_HAL_DMA` | `SPIF_TRANSFER_DMA`, whose reads now return at once: see [DMA](#dma) |
| `SPIF_RTOS` | `OSAL_RTOS` |
| `SPIF_RTOS_DISABLE` | `OSAL_RTOS_NONE` |
| `SPIF_DEBUG` | gone |

The page, sector and block reads and writes used to cut a call short at the end of their page, sector or block. `spif_read()` and `spif_write()` go on for as long as `size` says. Writes and erases used to wait inside the call, and now return at once: follow each with `spif_wait()`, or with `spif_is_busy()` from a state machine. See [Jobs](#jobs-spif_is_busy-and-spif_wait).

The one to watch: the functions used to return `true` when they worked, and now return `SPIF_ERR_NONE`, which is 0. The compiler finds every renamed function for you, but not this:

```c
if (SPIF_ReadAddress(&h, 0, data, 16))                /* was */
if (spif_read(&h, 0, data, 16) == SPIF_ERR_NONE)      /* is now */
if (spif_read(&h, 0, data, 16))                       /* compiles, and means "if it failed" */
```

With an RTOS, spif is now called from a thread only, as [above](#with-an-rtos).

You also no longer need CubeMX's "Generate peripheral initialization as a pair of .c/.h files per peripheral": `spif.h` includes `main.h` now, not `spi.h`.

---

## 🛠️ Troubleshooting

**`spif_init()` returns `SPIF_ERR_CHIP`.** The chip's answer to the JEDEC ID command is in `flash.manufacturer` and `flash.memory_type`. Both `0xFF`, or both `0x00`, means nothing answered: check MISO and MOSI are not swapped, the chip select pin, the supply, the SPI mode (0 or 3), and that WP and HOLD are tied high. A real maker there, such as `0xBF` with `0x25` for an SST25, means a chip spif does not support.

**`spif_init()` returns `SPIF_ERR_PROTECTED`.** See [Write protection](#write-protection). The bits are usually left by a programmer, or by earlier firmware.

**Data reads back wrong after a write.** The sector was not erased first. See [Erase before you write](#erase-before-you-write).

**`SPIF_ERR_INVALID` from an erase.** The chip's sectors are bigger than what was asked: a Spansion FL-S or an old M25P has no 4 KB erase, and the ones with 256 KB sectors no 64 KB one either. Check `flash.sector_size`.

**With DMA, every read or write ends in `SPIF_ERR_TIMEOUT`.** The DMA never finished, almost always because the SPI global interrupt is off. Check the [DMA setup](#dma).

**`SPIF_ERR_MUTEX` from `spif_init()`.** The RTOS could not create the mutex, which almost always means its heap is full. Make `configTOTAL_HEAP_SIZE` bigger in CubeMX's FreeRTOS settings.

---

## 🤝 Contributing

Bug reports and pull requests are welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for the style rules and how to run the tests. Nothing to sign, just open a pull request.

---

## 💖 Support

I write these libraries in my own time and give them away, because good tools should be easy to get. If this one saved you an afternoon, there are two things that genuinely help:

**⭐ Star the repo.** It costs you one click, it helps other engineers find the library, and it is the main reason I keep going.

**☕ [Buy me a coffee on Ko-fi](https://ko-fi.com/nimaltd).** Any amount is a real motivation to keep writing, documenting and maintaining this work.

[![GitHub](https://img.shields.io/badge/GitHub-Follow-black?style=for-the-badge&logo=github)](https://github.com/NimaLTD)
[![YouTube](https://img.shields.io/badge/YouTube-Subscribe-red?style=for-the-badge&logo=youtube)](https://youtube.com/@nimaltd)
[![Instagram](https://img.shields.io/badge/Instagram-Follow-purple?style=for-the-badge&logo=instagram)](https://instagram.com/github.nimaltd)
[![LinkedIn](https://img.shields.io/badge/LinkedIn-Connect-blue?style=for-the-badge&logo=linkedin)](https://linkedin.com/in/nimaltd)
[![Email](https://img.shields.io/badge/Email-Contact-red?style=for-the-badge&logo=gmail)](mailto:nima.askari@gmail.com)
[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-orange?style=for-the-badge&logo=ko-fi)](https://ko-fi.com/nimaltd)

---

## 📜 License

Apache License 2.0. See [LICENSE.md](LICENSE.md).

You are free to use this in commercial and closed source products. What the license asks in return is that you keep the copyright notice and pass along the [NOTICE](NOTICE) file, so the credit travels with the code.

The test folder vendors [Unity](https://github.com/ThrowTheSwitch/Unity) under its own MIT license, kept in [test/unity/LICENSE.txt](test/unity/LICENSE.txt), and [littlefs](https://github.com/littlefs-project/littlefs) under its own BSD-3-Clause license, kept in [test/littlefs/LICENSE.md](test/littlefs/LICENSE.md). Both are only used for testing and are not part of what you flash to a device.
