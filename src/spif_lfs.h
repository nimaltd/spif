/**
 * @file        spif_lfs.h
 * @brief       LittleFS on a flash chip that spif drives.
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
 * @note        Installed with the littlefs option of spif. A block is one
 *              sector, and the whole chip is the file system.
 */

#ifndef SPIF_LFS_H
#define SPIF_LFS_H

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <stdint.h>

#include "lfs.h"
#include "spif.h"

/*
 * ****************************************************************************************************
 * Configuration checks
 * ****************************************************************************************************
*/

/* Both are optional, and set in spif_config.h only to change them. */

/* Bytes in each of littlefs's two caches, read and program, which this handle
   holds. A power of two from 16 to the chip's sector size. Bigger means fewer
   transfers and more RAM, smaller the other way round. */
#ifndef SPIF_LFS_CACHE_SIZE
#define SPIF_LFS_CACHE_SIZE         SPIF_PAGE_SIZE
#elif (SPIF_LFS_CACHE_SIZE < 16U) || ((SPIF_LFS_CACHE_SIZE & (SPIF_LFS_CACHE_SIZE - 1U)) != 0U)
#error "SPIF_LFS_CACHE_SIZE must be a power of two, 16 or more"
#endif

/* Bytes of littlefs's map of free blocks, one bit a block. Each byte covers
   8 blocks, so 16 bytes cover 128 sectors, 512 KB of a chip with 4 KB ones.
   A multiple of 8. */
#ifndef SPIF_LFS_LOOKAHEAD_SIZE
#define SPIF_LFS_LOOKAHEAD_SIZE     16U
#elif (SPIF_LFS_LOOKAHEAD_SIZE == 0U) || ((SPIF_LFS_LOOKAHEAD_SIZE % 8U) != 0U)
#error "SPIF_LFS_LOOKAHEAD_SIZE must be a multiple of 8, and more than 0"
#endif

#ifdef __cplusplus
extern "C"
{
#endif

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief A file system on one chip. Declare one per chip and hand it to spif_lfs_init().
 */
typedef struct
{
    struct lfs_config cfg;                                           /**< Hand to lfs_mount(). */
    spif_t            *flash;                                        /**< The chip it is on.   */
#ifdef LFS_THREADSAFE
    osal_mutex_t      mutex;                                         /**< One thread at once.  */
#endif
    uint32_t          read_buffer[SPIF_LFS_CACHE_SIZE / 4U];         /**< Read cache.          */
    uint32_t          prog_buffer[SPIF_LFS_CACHE_SIZE / 4U];         /**< Program cache.       */
    uint32_t          lookahead_buffer[SPIF_LFS_LOOKAHEAD_SIZE / 4U]; /**< Free block map.      */

} spif_lfs_t;

/*
 * ****************************************************************************************************
 * Public function prototypes
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Fill in a file system for a chip spif_init() accepted. Then lfs_mount(&lfs, &handle->cfg).
 */
spif_err_t spif_lfs_init(spif_lfs_t *handle, spif_t *flash);

#ifdef __cplusplus
}
#endif

#endif /* SPIF_LFS_H */
