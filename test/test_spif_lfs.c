/**
 * @file        test_spif_lfs.c
 * @brief       Host unit tests for the LittleFS port, built on Unity.
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
 * @note        Runs on a PC. The port is tested here, not spif: spif's calls
 *              land on a fake that keeps the chips in RAM. Like spif with DMA,
 *              the fake only finishes a read, a write or an erase when
 *              spif_wait() is called, so a port that forgot to wait would
 *              find nothing read and nothing written. It programs as a NOR
 *              chip does, clearing bits only, and counts anything a careful
 *              port would never do. The real littlefs runs on top, from
 *              test/littlefs, with its asserts on.
 *
 *              Built twice, without LFS_THREADSAFE and with it.
 */

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "spif_lfs.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* Each fake chip: 64 sectors of 4 KB. */
#define CHIP_SECTOR         0x1000U
#define CHIP_BYTES          (64U * CHIP_SECTOR)

/* How many chips the fake has. */
#define CHIPS               2

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief What a fake chip is doing.
 */
typedef enum
{
    JOB_NONE = 0, /**< Nothing.                          */
    JOB_READ,     /**< A read, to land at spif_wait().   */
    JOB_WRITE,    /**< A write, to land at spif_wait().  */
    JOB_ERASE,    /**< An erase, to land at spif_wait(). */

} job_t;

/*****************************************************************************************************/
/**
 * @brief One fake chip, and what was done to it.
 */
typedef struct
{
    spif_t        flash;              /**< The handle the port is given.            */
    uint8_t       memory[CHIP_BYTES]; /**< What the chip holds.                     */
    job_t         job;                /**< The job started and not yet waited for.  */
    uint32_t      job_address;        /**< Where it starts.                         */
    uint8_t       *job_read;          /**< Where a read lands.                      */
    const uint8_t *job_write;         /**< What a write programs.                   */
    size_t        job_len;            /**< Bytes in it.                             */
    spif_err_t    result;             /**< What spif_wait() reports.                */
    int           started;            /**< Jobs started.                            */
    int           finished;           /**< Jobs spif_wait() finished.               */
    int           waits;              /**< Calls to spif_wait().                    */
    int           faults;             /**< Things a careful port never does.        */

} chip_t;

/*
 * ****************************************************************************************************
 * Global variables
 * ****************************************************************************************************
*/

static chip_t     chips[CHIPS];

/* What the next spif call answers: SPIF_ERR_NONE to start the job, anything
   else to refuse it. job_ends_with is what the job reports when it ends. */
static spif_err_t refuse_with   = SPIF_ERR_NONE;
static spif_err_t job_ends_with = SPIF_ERR_NONE;

/* The fake osal. */
static bool       mutex_create_fails = false;
static osal_err_t lock_answer        = OSAL_ERR_NONE;
static int        mutex_creates      = 0;
static int        locks              = 0;
static int        unlocks            = 0;
static uint32_t   lock_wait          = 0U;

/* assert_param, as CubeMX builds it with Enable Full Assert. */
static jmp_buf    assert_return;
static bool       assert_expected = false;

/* The file system on the first chip, and littlefs's own state for it. */
static spif_lfs_t fs;
static lfs_t      lfs;

/*
 * ****************************************************************************************************
 * Private function prototypes
 * ****************************************************************************************************
*/

static chip_t *chip_of(const spif_t *handle);
static bool   chip_start(chip_t *chip, uint32_t address, size_t len);
static int    file_put(lfs_t *fsys, const char *name, uint32_t value);
static int    file_get(lfs_t *fsys, const char *name, uint32_t *value);

/*
 * ****************************************************************************************************
 * The fake spif
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Start a fake read, which lands at spif_wait().
 *
 * @param[in,out] handle   The chip.
 * @param[in]     address  First byte.
 * @param[out]    data     Where the bytes land.
 * @param[in]     len      Bytes.
 * @return SPIF_ERR_NONE once started, or what the test says to refuse with.
 */
spif_err_t spif_read(spif_t *handle, uint32_t address, uint8_t *data, size_t len)
{
    chip_t     *chip = chip_of(handle);
    spif_err_t err   = refuse_with;

    if ((err == SPIF_ERR_NONE) && chip_start(chip, address, len))
    {
        chip->job      = JOB_READ;
        chip->job_read = data;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Start a fake write, which lands at spif_wait().
 *
 * @param[in,out] handle   The chip.
 * @param[in]     address  First byte.
 * @param[in]     data     The bytes, which must stay as they are until then.
 * @param[in]     len      Bytes.
 * @return SPIF_ERR_NONE once started, or what the test says to refuse with.
 */
spif_err_t spif_write(spif_t *handle, uint32_t address, const uint8_t *data, size_t len)
{
    chip_t     *chip = chip_of(handle);
    spif_err_t err   = refuse_with;

    if ((err == SPIF_ERR_NONE) && chip_start(chip, address, len))
    {
        chip->job       = JOB_WRITE;
        chip->job_write = data;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Start a fake sector erase, which lands at spif_wait().
 *
 * @param[in,out] handle  The chip.
 * @param[in]     sector  Which sector.
 * @return SPIF_ERR_NONE once started, or what the test says to refuse with.
 */
spif_err_t spif_erase_sector(spif_t *handle, uint32_t sector)
{
    chip_t     *chip = chip_of(handle);
    spif_err_t err   = refuse_with;

    if ((err == SPIF_ERR_NONE) && chip_start(chip, sector * CHIP_SECTOR, CHIP_SECTOR))
    {
        chip->job = JOB_ERASE;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Finish the job a fake call started, and report how it ended.
 *
 * @param[in,out] handle  The chip.
 * @return What the chip's result is: the job's, or with no job, the one before.
 */
spif_err_t spif_wait(spif_t *handle)
{
    chip_t *chip = chip_of(handle);
    size_t i     = 0U;

    chip->waits++;

    if (chip->job != JOB_NONE)
    {
        for (i = 0U; i < chip->job_len; i++)
        {
            uint8_t *cell = &chip->memory[chip->job_address + i];

            if (chip->job == JOB_READ)
            {
                chip->job_read[i] = *cell;
            }
            else if (chip->job == JOB_WRITE)
            {
                /* A NOR chip only clears bits. littlefs only programs what it
                   erased, so anything else here is a wrong address. */
                if (*cell != 0xFFU)
                {
                    chip->faults++;
                }

                *cell &= chip->job_write[i];
            }
            else
            {
                *cell = 0xFFU;
            }
        }

        chip->job    = JOB_NONE;
        chip->result = job_ends_with;
        chip->finished++;
    }

    return chip->result;
}

/*
 * ****************************************************************************************************
 * The fake osal
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Create the fake mutex, unless the test says it cannot be made.
 *
 * @param[out] mutex  Mutex to create.
 * @return OSAL_ERR_NONE, or OSAL_ERR_MUTEX when the test says so.
 */
osal_err_t osal_mutex_create(osal_mutex_t *mutex)
{
    (void)mutex;

    mutex_creates++;

    return mutex_create_fails ? OSAL_ERR_MUTEX : OSAL_ERR_NONE;
}

/*****************************************************************************************************/
/**
 * @brief Take the fake mutex, answering as the test chose.
 *
 * @param[in,out] mutex       Mutex to take.
 * @param[in]     timeout_ms  How long the caller would wait.
 * @return What the test chose.
 */
osal_err_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms)
{
    (void)mutex;

    lock_wait = timeout_ms;

    if (lock_answer == OSAL_ERR_NONE)
    {
        locks++;
    }

    return lock_answer;
}

/*****************************************************************************************************/
/**
 * @brief Give the fake mutex back.
 *
 * @param[in,out] mutex  Mutex to give back.
 */
void osal_mutex_unlock(osal_mutex_t *mutex)
{
    (void)mutex;

    unlocks++;
}

/*****************************************************************************************************/
/**
 * @brief Sleep. Nothing here waits.
 *
 * @param[in] ms  How long.
 */
void osal_delay_ms(uint32_t ms)
{
    (void)ms;
}

/*****************************************************************************************************/
/**
 * @brief Where assert_param lands. Jumps back to the test that expected it.
 *
 * @param[in] file  Source file.
 * @param[in] line  Line in it.
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;

    if (!assert_expected)
    {
        TEST_FAIL_MESSAGE("assert_param stopped a valid call");
    }

    longjmp(assert_return, 1);
}

/*
 * ****************************************************************************************************
 * Set up and tear down
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Blank chips, as they leave the factory, and a fake that answers yes.
 */
void setUp(void)
{
    int i = 0;

    for (i = 0; i < CHIPS; i++)
    {
        (void)memset(&chips[i], 0, sizeof(chips[i]));
        (void)memset(chips[i].memory, 0xFF, sizeof(chips[i].memory));
        chips[i].flash.size        = CHIP_BYTES;
        chips[i].flash.sector_size = CHIP_SECTOR;
    }

    refuse_with        = SPIF_ERR_NONE;
    job_ends_with      = SPIF_ERR_NONE;
    mutex_create_fails = false;
    lock_answer        = OSAL_ERR_NONE;
    mutex_creates      = 0;
    locks              = 0;
    unlocks            = 0;
    lock_wait          = 0U;
    assert_expected    = false;
}

/*****************************************************************************************************/
/**
 * @brief Nothing to undo.
 */
void tearDown(void)
{
}

/*
 * ****************************************************************************************************
 * Tests: setting up
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief The config is filled in for the chip: a block a sector, the caches in the handle.
 */
static void test_init_fills_in_the_config(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    TEST_ASSERT_EQUAL_PTR(&fs, fs.cfg.context);
    TEST_ASSERT_EQUAL_PTR(&chips[0].flash, fs.flash);
    TEST_ASSERT_NOT_NULL(fs.cfg.read);
    TEST_ASSERT_NOT_NULL(fs.cfg.prog);
    TEST_ASSERT_NOT_NULL(fs.cfg.erase);
    TEST_ASSERT_NOT_NULL(fs.cfg.sync);
    TEST_ASSERT_EQUAL_UINT32(CHIP_SECTOR, fs.cfg.block_size);
    TEST_ASSERT_EQUAL_UINT32(64U, fs.cfg.block_count);
    TEST_ASSERT_EQUAL_UINT32(16U, fs.cfg.read_size);
    TEST_ASSERT_EQUAL_UINT32(SPIF_LFS_CACHE_SIZE, fs.cfg.prog_size);
    TEST_ASSERT_EQUAL_UINT32(SPIF_LFS_CACHE_SIZE, fs.cfg.cache_size);
    TEST_ASSERT_EQUAL_UINT32(SPIF_LFS_LOOKAHEAD_SIZE, fs.cfg.lookahead_size);
    TEST_ASSERT_EQUAL_INT32(500, fs.cfg.block_cycles);
    TEST_ASSERT_EQUAL_PTR(fs.read_buffer, fs.cfg.read_buffer);
    TEST_ASSERT_EQUAL_PTR(fs.prog_buffer, fs.cfg.prog_buffer);
    TEST_ASSERT_EQUAL_PTR(fs.lookahead_buffer, fs.cfg.lookahead_buffer);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, chips[0].started, "init touched the chip");
}

/*****************************************************************************************************/
/**
 * @brief What init leaves alone stays 0, which is littlefs's default, even over a used handle.
 */
static void test_init_clears_what_it_does_not_set(void)
{
    (void)memset(&fs, 0xA5, sizeof(fs));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    TEST_ASSERT_EQUAL_UINT32(0U, fs.cfg.name_max);
    TEST_ASSERT_EQUAL_UINT32(0U, fs.cfg.file_max);
    TEST_ASSERT_EQUAL_UINT32(0U, fs.cfg.metadata_max);
    TEST_ASSERT_EQUAL_UINT32(0U, fs.cfg.inline_max);
}

/*****************************************************************************************************/
/**
 * @brief A chip spif_init() did not accept has a size of 0, and is refused.
 */
static void test_a_chip_spif_init_refused_is_refused(void)
{
    chips[0].flash.size = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_lfs_init(&fs, &chips[0].flash));
}

/*****************************************************************************************************/
/**
 * @brief A sector the cache does not divide is refused, since littlefs's own assert is off.
 */
static void test_a_sector_the_cache_does_not_divide_is_refused(void)
{
    chips[0].flash.sector_size = SPIF_LFS_CACHE_SIZE + (SPIF_LFS_CACHE_SIZE / 2U);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_lfs_init(&fs, &chips[0].flash));
}

/*****************************************************************************************************/
/**
 * @brief A NULL is a bug in the caller, and assert_param stops it.
 */
static void test_a_null_pointer_is_stopped(void)
{
    int which = 0;

    for (which = 0; which < 2; which++)
    {
        bool stopped = true;

        assert_expected = true;

        if (setjmp(assert_return) == 0)
        {
            (void)spif_lfs_init((which == 0) ? NULL : &fs, (which == 0) ? &chips[0].flash : NULL);

            /* Back here means no assert stopped it. */
            stopped = false;
        }

        assert_expected = false;

        TEST_ASSERT_TRUE_MESSAGE(stopped, (which == 0) ? "a NULL handle" : "a NULL flash");
    }
}

/*
 * ****************************************************************************************************
 * Tests: a file system on it
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief A blank chip does not mount, as corrupt. That and only that is the time to format.
 */
static void test_a_blank_chip_mounts_as_corrupt(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    TEST_ASSERT_EQUAL_INT(LFS_ERR_CORRUPT, lfs_mount(&lfs, &fs.cfg));
}

/*****************************************************************************************************/
/**
 * @brief A file written, the file system unmounted and mounted again, is still there.
 */
static void test_a_file_is_kept_across_a_mount(void)
{
    uint32_t boots = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_format(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_mount(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_put(&lfs, "boots", 41U));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_unmount(&lfs));

    /* As at the next power up: a new handle over the same chip. */
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_mount(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_get(&lfs, "boots", &boots));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_unmount(&lfs));

    TEST_ASSERT_EQUAL_UINT32(41U, boots);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, chips[0].faults, "programmed a byte that was not erased");
}

/*****************************************************************************************************/
/**
 * @brief Every job the port starts, it waits for, before it starts the next.
 */
static void test_every_job_is_waited_for(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_format(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_mount(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_put(&lfs, "log", 7U));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_unmount(&lfs));

    TEST_ASSERT_GREATER_THAN_INT(0, chips[0].started);
    TEST_ASSERT_EQUAL_INT(chips[0].started, chips[0].finished);
    TEST_ASSERT_EQUAL_INT(JOB_NONE, chips[0].job);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, chips[0].faults, "started a job while one was running");
}

/*****************************************************************************************************/
/**
 * @brief Each call reaches the byte littlefs asked for: block times the sector, plus the offset.
 */
static void test_each_call_reaches_the_right_place(void)
{
    uint8_t       buffer[16];
    const uint8_t data[4] = { 0x12U, 0x34U, 0x56U, 0x78U };

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, fs.cfg.prog(&fs.cfg, 3U, 100U, data, sizeof(data)));
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &chips[0].memory[(3U * CHIP_SECTOR) + 100U], sizeof(data));

    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, fs.cfg.read(&fs.cfg, 3U, 96U, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_HEX8(0xFFU, buffer[3]);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(data, &buffer[4], sizeof(data));

    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, fs.cfg.erase(&fs.cfg, 3U));
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chips[0].memory[(3U * CHIP_SECTOR) + 100U]);
    TEST_ASSERT_EQUAL_UINT32(3U * CHIP_SECTOR, chips[0].job_address);
}

/*****************************************************************************************************/
/**
 * @brief A call spif refuses is an error, with no wait: that would report someone else's job.
 */
static void test_a_refused_call_is_an_error(void)
{
    uint8_t       buffer[16];
    const uint8_t data[4] = { 0U, 0U, 0U, 0U };

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    /* Another job is running, and will end well. */
    refuse_with = SPIF_ERR_BUSY;

    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, fs.cfg.read(&fs.cfg, 0U, 0U, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, fs.cfg.prog(&fs.cfg, 0U, 0U, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, fs.cfg.erase(&fs.cfg, 0U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, chips[0].waits, "waited for a job that never started");
}

/*****************************************************************************************************/
/**
 * @brief A job that starts and then fails is an error too.
 */
static void test_a_job_that_fails_is_an_error(void)
{
    uint8_t       buffer[16];
    const uint8_t data[4] = { 0U, 0U, 0U, 0U };

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    job_ends_with = SPIF_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, fs.cfg.read(&fs.cfg, 0U, 0U, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, fs.cfg.prog(&fs.cfg, 0U, 0U, data, sizeof(data)));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, fs.cfg.erase(&fs.cfg, 0U));
}

/*****************************************************************************************************/
/**
 * @brief sync has nothing left to do, and does not touch the chip.
 */
static void test_sync_has_nothing_to_do(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, fs.cfg.sync(&fs.cfg));
    TEST_ASSERT_EQUAL_INT(0, chips[0].started);
    TEST_ASSERT_EQUAL_INT(0, chips[0].waits);
}

/*****************************************************************************************************/
/**
 * @brief Two chips, two file systems, and neither writes to the other.
 */
static void test_two_chips_keep_to_themselves(void)
{
    static spif_lfs_t other;
    static lfs_t      other_lfs;
    uint32_t          first  = 0U;
    uint32_t          second = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&other, &chips[1].flash));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_format(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_format(&other_lfs, &other.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_mount(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_mount(&other_lfs, &other.cfg));

    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_put(&lfs, "n", 1U));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_put(&other_lfs, "n", 2U));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_get(&lfs, "n", &first));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_get(&other_lfs, "n", &second));

    TEST_ASSERT_EQUAL_UINT32(1U, first);
    TEST_ASSERT_EQUAL_UINT32(2U, second);
    TEST_ASSERT_EQUAL_INT(0, chips[0].faults + chips[1].faults);
}

/*
 * ****************************************************************************************************
 * Tests: threads
 * ****************************************************************************************************
*/

#ifdef LFS_THREADSAFE
/*****************************************************************************************************/
/**
 * @brief With LFS_THREADSAFE, init creates a mutex and gives littlefs the lock.
 */
static void test_threadsafe_init_creates_the_mutex(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    TEST_ASSERT_EQUAL_INT(1, mutex_creates);
    TEST_ASSERT_NOT_NULL(fs.cfg.lock);
    TEST_ASSERT_NOT_NULL(fs.cfg.unlock);
}

/*****************************************************************************************************/
/**
 * @brief A mutex the RTOS cannot make is reported.
 */
static void test_a_mutex_the_rtos_cannot_make_is_reported(void)
{
    mutex_create_fails = true;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_MUTEX, spif_lfs_init(&fs, &chips[0].flash));
}

/*****************************************************************************************************/
/**
 * @brief Every littlefs call takes the lock and gives it back, waiting as long as spif would.
 */
static void test_every_call_takes_and_gives_back_the_lock(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_format(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, lfs_mount(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(LFS_ERR_OK, file_put(&lfs, "n", 1U));

    TEST_ASSERT_GREATER_THAN_INT(3, locks);
    TEST_ASSERT_EQUAL_INT(locks, unlocks);
    TEST_ASSERT_EQUAL_UINT32(SPIF_TIMEOUT_MUTEX_MS, lock_wait);
}

/*****************************************************************************************************/
/**
 * @brief A lock another thread keeps fails the call, and the chip is not touched.
 */
static void test_a_lock_not_had_fails_the_call(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));

    lock_answer = OSAL_ERR_TIMEOUT;

    TEST_ASSERT_EQUAL_INT(LFS_ERR_IO, lfs_format(&lfs, &fs.cfg));
    TEST_ASSERT_EQUAL_INT(0, chips[0].started);
    TEST_ASSERT_EQUAL_INT(0, unlocks);
}
#else
/*****************************************************************************************************/
/**
 * @brief Without LFS_THREADSAFE there is no lock, so no mutex is made.
 */
static void test_no_mutex_without_threadsafe(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_lfs_init(&fs, &chips[0].flash));
    TEST_ASSERT_EQUAL_INT(0, mutex_creates);
}
#endif

/*
 * ****************************************************************************************************
 * Private function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief The fake chip behind a handle.
 *
 * @param[in] handle  A handle the tests made.
 * @return Its chip.
 */
static chip_t *chip_of(const spif_t *handle)
{
    int i = 0;

    for (i = 0; i < (CHIPS - 1); i++)
    {
        if (handle == &chips[i].flash)
        {
            break;
        }
    }

    TEST_ASSERT_EQUAL_PTR_MESSAGE(&chips[i].flash, handle, "a handle the tests never made");

    return &chips[i];
}

/*****************************************************************************************************/
/**
 * @brief Start a job, unless one is running or it would leave the chip.
 *
 * @param[in,out] chip     The chip.
 * @param[in]     address  First byte.
 * @param[in]     len      Bytes.
 * @return true once started.
 */
static bool chip_start(chip_t *chip, uint32_t address, size_t len)
{
    bool started = false;

    /* spif would refuse it with SPIF_ERR_BUSY. littlefs waits for each step,
       so only a port that forgot to wait gets here. */
    if (chip->job != JOB_NONE)
    {
        chip->faults++;
    }
    else if ((address + len) > CHIP_BYTES)
    {
        chip->faults++;
    }
    else
    {
        chip->job_address = address;
        chip->job_len     = len;
        chip->started++;
        started = true;
    }

    return started;
}

/*****************************************************************************************************/
/**
 * @brief Write a number to a file, as a user would.
 *
 * @param[in,out] fsys   A mounted file system.
 * @param[in]     name   The file.
 * @param[in]     value  The number.
 * @return LFS_ERR_OK, or the first littlefs error.
 */
static int file_put(lfs_t *fsys, const char *name, uint32_t value)
{
    lfs_file_t file;
    int        err = lfs_file_open(fsys, &file, name, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);

    if (err == LFS_ERR_OK)
    {
        lfs_ssize_t wrote = lfs_file_write(fsys, &file, &value, sizeof(value));

        err = lfs_file_close(fsys, &file);

        if (wrote != (lfs_ssize_t)sizeof(value))
        {
            err = LFS_ERR_IO;
        }
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Read a number back from a file.
 *
 * @param[in,out] fsys   A mounted file system.
 * @param[in]     name   The file.
 * @param[out]    value  The number.
 * @return LFS_ERR_OK, or the first littlefs error.
 */
static int file_get(lfs_t *fsys, const char *name, uint32_t *value)
{
    lfs_file_t file;
    int        err = lfs_file_open(fsys, &file, name, LFS_O_RDONLY);

    if (err == LFS_ERR_OK)
    {
        lfs_ssize_t got = lfs_file_read(fsys, &file, value, sizeof(*value));

        err = lfs_file_close(fsys, &file);

        if (got != (lfs_ssize_t)sizeof(*value))
        {
            err = LFS_ERR_IO;
        }
    }

    return err;
}

/*
 * ****************************************************************************************************
 * Runner
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Run every test.
 *
 * @return The number of failures.
 */
int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_init_fills_in_the_config);
    RUN_TEST(test_init_clears_what_it_does_not_set);
    RUN_TEST(test_a_chip_spif_init_refused_is_refused);
    RUN_TEST(test_a_sector_the_cache_does_not_divide_is_refused);
    RUN_TEST(test_a_null_pointer_is_stopped);
    RUN_TEST(test_a_blank_chip_mounts_as_corrupt);
    RUN_TEST(test_a_file_is_kept_across_a_mount);
    RUN_TEST(test_every_job_is_waited_for);
    RUN_TEST(test_each_call_reaches_the_right_place);
    RUN_TEST(test_a_refused_call_is_an_error);
    RUN_TEST(test_a_job_that_fails_is_an_error);
    RUN_TEST(test_sync_has_nothing_to_do);
    RUN_TEST(test_two_chips_keep_to_themselves);
#ifdef LFS_THREADSAFE
    RUN_TEST(test_threadsafe_init_creates_the_mutex);
    RUN_TEST(test_a_mutex_the_rtos_cannot_make_is_reported);
    RUN_TEST(test_every_call_takes_and_gives_back_the_lock);
    RUN_TEST(test_a_lock_not_had_fails_the_call);
#else
    RUN_TEST(test_no_mutex_without_threadsafe);
#endif

    return UNITY_END();
}
