/**
 * @file        spif_lfs.c
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
 */

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include "spif_lfs.h"
#include <string.h>

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* The smallest read littlefs makes. A NOR chip reads any byte, so this only
   keeps it from asking for a few bytes at a time, each with a command and an
   address in front of it. */
#define SPIF_LFS_READ_SIZE          16U

/* Erases of a metadata block before littlefs moves it on, which spreads the
   wear. littlefs suggests 100 to 1000, and a NOR sector takes 100000. */
#define SPIF_LFS_BLOCK_CYCLES       500

/*
 * ****************************************************************************************************
 * Private function prototypes
 * ****************************************************************************************************
*/

static int spif_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                         lfs_size_t size);
static int spif_lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                         const void *buffer, lfs_size_t size);
static int spif_lfs_erase(const struct lfs_config *c, lfs_block_t block);
static int spif_lfs_sync(const struct lfs_config *c);
#ifdef LFS_THREADSAFE
static int spif_lfs_lock(const struct lfs_config *c);
static int spif_lfs_unlock(const struct lfs_config *c);
#endif
static int spif_lfs_finish(spif_t *flash, spif_err_t err);

/*
 * ****************************************************************************************************
 * Public function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Fill in a file system for a chip spif_init() accepted.
 *
 * @param[out] handle  The file system. Hand handle->cfg to lfs_mount() and lfs_format().
 * @param[in]  flash   Handle from spif_init(), for the chip it lives on.
 * @return SPIF_ERR_NONE, SPIF_ERR_INVALID for a chip spif_init() did not accept or a cache that
 *         does not divide its sector, or SPIF_ERR_MUTEX when the RTOS could not create the mutex.
 */
spif_err_t spif_lfs_init(spif_lfs_t *handle, spif_t *flash)
{
    spif_err_t err = SPIF_ERR_INVALID;

    assert_param(handle != NULL);
    assert_param(flash != NULL);

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* A chip spif_init() did not accept has a size of 0. A cache that does
           not divide a sector evenly is one littlefs cannot use, and with its
           asserts off, as lfs_defines.h ships, nothing else would say so. */
        if ((flash->size == 0U) || ((flash->sector_size % SPIF_LFS_CACHE_SIZE) != 0U))
        {
            break;
        }

        /* Every field this does not set is 0, which is littlefs's default. */
        (void)memset(handle, 0, sizeof(*handle));
        handle->flash = flash;

        /* How littlefs reaches the chip. Each call brings context back, so two
           chips can each have a file system of their own. */
        handle->cfg.context = handle;
        handle->cfg.read    = spif_lfs_read;
        handle->cfg.prog    = spif_lfs_prog;
        handle->cfg.erase   = spif_lfs_erase;
        handle->cfg.sync    = spif_lfs_sync;

        /* A block is one sector, the smallest part the chip erases, so the
           file system fills the chip whatever its size. */
        handle->cfg.block_size  = flash->sector_size;
        handle->cfg.block_count = flash->size / flash->sector_size;

        /* littlefs writes a whole cache at a time, so its program size is the
           cache. A NOR chip takes any length, and spif splits one at a page
           boundary itself. */
        handle->cfg.read_size      = SPIF_LFS_READ_SIZE;
        handle->cfg.prog_size      = SPIF_LFS_CACHE_SIZE;
        handle->cfg.cache_size     = SPIF_LFS_CACHE_SIZE;
        handle->cfg.lookahead_size = SPIF_LFS_LOOKAHEAD_SIZE;
        handle->cfg.block_cycles   = SPIF_LFS_BLOCK_CYCLES;

        /* The caches live in this handle, so mounting takes nothing from the
           heap. A file still takes its own cache from lfs_file_open(), or the
           buffer given to lfs_file_opencfg(). */
        handle->cfg.read_buffer      = handle->read_buffer;
        handle->cfg.prog_buffer      = handle->prog_buffer;
        handle->cfg.lookahead_buffer = handle->lookahead_buffer;

        err = SPIF_ERR_NONE;

#ifdef LFS_THREADSAFE
        /* littlefs takes this around every call, so one thread at a time is in
           the file system. spif's own mutex covers one chip access, not the
           several a file operation makes. */
        handle->cfg.lock   = spif_lfs_lock;
        handle->cfg.unlock = spif_lfs_unlock;

        if (osal_mutex_create(&handle->mutex) != OSAL_ERR_NONE)
        {
            err = SPIF_ERR_MUTEX;
        }
#endif
    }
    while (false);

    return err;
}

/*
 * ****************************************************************************************************
 * Private function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Read from the chip, for littlefs.
 *
 * @param[in]  c       The file system's config, its context the handle.
 * @param[in]  block   Block, which is the sector, to read from.
 * @param[in]  off     Byte in the block to start at.
 * @param[out] buffer  Where the bytes go.
 * @param[in]  size    Bytes to read.
 * @return LFS_ERR_OK, or LFS_ERR_IO when spif refused or failed.
 */
static int spif_lfs_read(const struct lfs_config *c, lfs_block_t block, lfs_off_t off, void *buffer,
                         lfs_size_t size)
{
    spif_lfs_t *handle = (spif_lfs_t *)c->context;
    uint32_t   address = (block * c->block_size) + off;

    return spif_lfs_finish(handle->flash,
                           spif_read(handle->flash, address, (uint8_t *)buffer, size));
}

/*****************************************************************************************************/
/**
 * @brief Write to the chip, for littlefs, into a block it erased.
 *
 * @param[in] c       The file system's config, its context the handle.
 * @param[in] block   Block, which is the sector, to write to.
 * @param[in] off     Byte in the block to start at.
 * @param[in] buffer  The bytes to write.
 * @param[in] size    Bytes to write.
 * @return LFS_ERR_OK, or LFS_ERR_IO when spif refused or failed.
 */
static int spif_lfs_prog(const struct lfs_config *c, lfs_block_t block, lfs_off_t off,
                         const void *buffer, lfs_size_t size)
{
    spif_lfs_t *handle = (spif_lfs_t *)c->context;
    uint32_t   address = (block * c->block_size) + off;

    return spif_lfs_finish(handle->flash,
                           spif_write(handle->flash, address, (const uint8_t *)buffer, size));
}

/*****************************************************************************************************/
/**
 * @brief Erase one block, for littlefs.
 *
 * @param[in] c      The file system's config, its context the handle.
 * @param[in] block  Block to erase.
 * @return LFS_ERR_OK, or LFS_ERR_IO when spif refused or failed.
 */
static int spif_lfs_erase(const struct lfs_config *c, lfs_block_t block)
{
    spif_lfs_t *handle = (spif_lfs_t *)c->context;

    /* A block is one sector, so the numbers are the same. */
    return spif_lfs_finish(handle->flash, spif_erase_sector(handle->flash, block));
}

/*****************************************************************************************************/
/**
 * @brief Make what was written stay, for littlefs.
 *
 * @param[in] c  The file system's config.
 * @return LFS_ERR_OK.
 */
static int spif_lfs_sync(const struct lfs_config *c)
{
    (void)c;

    /* Nothing is left to do: every write and erase above waited until the
       chip had finished it, and a NOR chip keeps no cache of its own. */
    return LFS_ERR_OK;
}

#ifdef LFS_THREADSAFE
/*****************************************************************************************************/
/**
 * @brief Take the file system, for littlefs, before each of its calls.
 *
 * @param[in] c  The file system's config, its context the handle.
 * @return LFS_ERR_OK, or LFS_ERR_IO when another thread kept it for the whole wait.
 */
static int spif_lfs_lock(const struct lfs_config *c)
{
    spif_lfs_t *handle = (spif_lfs_t *)c->context;

    /* As long as spif waits for the chip, which as it ships is for ever. */
    osal_err_t lock = osal_mutex_lock(&handle->mutex, SPIF_TIMEOUT_MUTEX_MS);

    return (lock == OSAL_ERR_NONE) ? LFS_ERR_OK : LFS_ERR_IO;
}

/*****************************************************************************************************/
/**
 * @brief Give the file system back, for littlefs, after each of its calls.
 *
 * @param[in] c  The file system's config, its context the handle.
 * @return LFS_ERR_OK.
 */
static int spif_lfs_unlock(const struct lfs_config *c)
{
    spif_lfs_t *handle = (spif_lfs_t *)c->context;

    osal_mutex_unlock(&handle->mutex);

    return LFS_ERR_OK;
}
#endif

/*****************************************************************************************************/
/**
 * @brief Wait for the job a spif call started, and turn how it ended into a littlefs error.
 *
 * @param[in,out] flash  The chip.
 * @param[in]     err    What the spif call returned.
 * @return LFS_ERR_OK when the job started and ended well, otherwise LFS_ERR_IO.
 */
static int spif_lfs_finish(spif_t *flash, spif_err_t err)
{
    spif_err_t result = err;

    /* Only a call that started a job has one to wait for. A call refused
       because another job was running started nothing, and spif_wait() would
       wait for that other job and report how it ended, as if it were this one.
       littlefs waits for each step before the next, so it can only meet a job
       the application started and left running. */
    if (result == SPIF_ERR_NONE)
    {
        result = spif_wait(flash);
    }

    return (result == SPIF_ERR_NONE) ? LFS_ERR_OK : LFS_ERR_IO;
}
