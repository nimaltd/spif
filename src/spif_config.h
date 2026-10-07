/**
 * @file        spif_config.h
 * @brief       Build time configuration for the spif library.
 * @version     3.0.0
 *
 * @author      Nima Askari (NimaLTD)
 * @email       nima.askari@gmail.com
 * @github      https://www.github.com/nimaltd
 * @linkedin    https://www.linkedin.com/in/nimaltd
 * @youtube     https://www.youtube.com/@nimaltd
 * @instagram   https://instagram.com/github.nimaltd
 *
 * @copyright   (c) 2026 Nima Askari (NimaLTD)
 *              SPDX-License-Identifier: Apache-2.0
 *              See LICENSE.md in the project root for the full license text.
 *
 * @note        Your settings go between the USER CODE markers. The installer
 *              replaces the rest of this file on every update and keeps what
 *              is between them, so a setting you changed is never lost.
 */

#ifndef SPIF_CONFIG_H
#define SPIF_CONFIG_H

/*
 * ****************************************************************************************************
 * Configuration
 * ****************************************************************************************************
*/

/* USER CODE BEGIN SPIF_CONFIGURATION */

/* How spif_read() and spif_write() move their data. One of:
     SPIF_TRANSFER_POLLING  the CPU moves it, and a read returns with the data
     SPIF_TRANSFER_DMA      the DMA moves it, and a read is a job like a write:
                            it returns once started, and spif_is_busy() or
                            spif_wait() says when it has finished
   With DMA, give the SPI a DMA channel for TX and one for RX in CubeMX, and
   turn on the SPI global interrupt. */
#define SPIF_TRANSFER                   SPIF_TRANSFER_POLLING

/* How long each step may take, in milliseconds, before it ends with
   SPIF_ERR_TIMEOUT. Each is the longest time in a Winbond W25Q128JV's
   datasheet plus half again. Check your chip's datasheet and fit them to it:
   a bigger or an older chip can take longer. */

/* Waiting for another thread to finish with the chip. HAL_MAX_DELAY waits as
   long as it takes, which is safe: whoever has the chip gives it back within
   the times below. */
#define SPIF_TIMEOUT_MUTEX_MS           HAL_MAX_DELAY

/* One SPI transfer of up to 32 KB. That takes 13 ms at 21 MHz, and 262 ms at
   1 MHz, the slowest clock this allows for. */
#define SPIF_TIMEOUT_TRANSFER_MS        400U

/* Storing one 256 byte page: 3 ms at most. */
#define SPIF_TIMEOUT_PAGE_MS            5U

/* Erasing one sector, the smallest part the chip erases: 400 ms at most for
   4 KB. A chip with 64 KB or 256 KB sectors takes as long as for a block. */
#define SPIF_TIMEOUT_SECTOR_MS          600U

/* Erasing a 32 KB block: 1.6 s at most. */
#define SPIF_TIMEOUT_BLOCK32_MS         2400U

/* Erasing a 64 KB block: 2 s at most. */
#define SPIF_TIMEOUT_BLOCK_MS           3000U

/* Erasing the whole chip, for each MB it holds: 200 s at most for 16 MB. */
#define SPIF_TIMEOUT_CHIP_PER_MB_MS     18750U

/* USER CODE END SPIF_CONFIGURATION */

#endif /* SPIF_CONFIG_H */
