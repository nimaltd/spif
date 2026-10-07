/**
 * @file        spif.c
 * @brief       Driver for SPI NOR flash, W25Qxx and compatible, on the STM32 HAL.
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

#include "spif.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* How often spif_wait() asks whether a job has finished. */
#define SPIF_POLL_MS                1U

/* A chip takes no command for a while after power up: datasheets give up to
   10 ms. Counted from reset, so only a call made straight after it waits. */
#define SPIF_POWER_UP_MS            20U

/* Writing a status register, done only to unlock an AT25DF or an ESMT chip:
   15 ms at most on any chip, plus half again. */
#define SPIF_TIMEOUT_STATUS_MS      25U

/* The HAL counts a transfer in a uint16_t, so a longer one goes in pieces. */
#define SPIF_TRANSFER_LIMIT         0x8000U

/* Three address bytes reach 16 MB. A bigger chip needs a fourth. */
#define SPIF_3BYTE_LIMIT            0x1000000U

/* The biggest single die. A chip over this is two or four of them. */
#define SPIF_DIE_SIZE               0x4000000U

/* Sector sizes: 4 KB on nearly every chip, bigger on some old ones. */
#define SPIF_SECTOR_4K              0x1000U
#define SPIF_SECTOR_32K             0x8000U
#define SPIF_SECTOR_64K             0x10000U
#define SPIF_SECTOR_256K            0x40000U

/* The longest command: an opcode, then five bytes of address or dummy. */
#define SPIF_COMMAND_MAX            6U

/* JEDEC ID bytes read: maker, memory type and capacity, then three more that
   tell Spansion's sector sizes apart. */
#define SPIF_ID_BYTES               6U

/* Register bits. */
#define SPIF_STATUS_BUSY            0x01U   /* Status 1: programming or erasing.          */
#define SPIF_STATUS_BP              0x1CU   /* Status 1: BP0 to BP2, the same on all.     */
#define SPIF_STATUS_SWP             0x0CU   /* AT25DF status: sectors protected.          */
#define SPIF_STATUS_ERRORS          0x60U   /* Spansion status: program or erase refused. */
#define SPIF_FLAG_READY             0x80U   /* Micron flag status: not busy.              */
#define SPIF_FLAG_ERRORS            0x32U   /* Micron flag status: refused, or failed.    */
#define SPIF_CONFIG_BPNV            0x08U   /* SST26 config: no block locked for ever.    */

/* Families of chips that need something the usual ones do not. */
#define SPIF_FAMILY_STANDARD        0U  /* Winbond, and everything like it.            */
#define SPIF_FAMILY_MICRON          1U  /* N25Q, MT25Q: a flag status register.        */
#define SPIF_FAMILY_SPANSION        2U  /* FL-S, FL-P, SL: big sectors, sticky errors. */
#define SPIF_FAMILY_SST26           3U  /* Locked at power up, unlocked with 98h.      */
#define SPIF_FAMILY_UNPROTECT       4U  /* AT25DF, ESMT: locked, unlocked by 00h.      */

/* Infineon's Semper chips, which this driver refuses. */
#define SPIF_MANUFACTURER_SEMPER    0x34U

/* What a job is doing. */
#define SPIF_JOB_IDLE               0U
#define SPIF_JOB_READ               1U  /* DMA bringing data in.  */
#define SPIF_JOB_SEND               2U  /* DMA sending a page.    */
#define SPIF_JOB_PROGRAM            3U  /* The chip storing it.   */
#define SPIF_JOB_ERASE              4U  /* The chip erasing.      */

/* Commands, the same on every chip this driver knows unless said otherwise. */
#define SPIF_CMD_WRITE_STATUS       0x01U
#define SPIF_CMD_PROGRAM            0x02U
#define SPIF_CMD_WRITE_DISABLE      0x04U
#define SPIF_CMD_READ_STATUS        0x05U
#define SPIF_CMD_WRITE_ENABLE       0x06U
#define SPIF_CMD_FAST_READ          0x0BU
#define SPIF_CMD_FAST_READ_4B       0x0CU   /* Spansion */
#define SPIF_CMD_PROGRAM_4B         0x12U   /* Spansion */
#define SPIF_CMD_ERASE_SECTOR       0x20U
#define SPIF_CMD_ERASE_SECTOR_4B    0x21U   /* Spansion */
#define SPIF_CMD_CLEAR_STATUS       0x30U   /* Spansion */
#define SPIF_CMD_READ_CONFIG        0x35U   /* SST26    */
#define SPIF_CMD_UNIQUE_ID          0x4BU
#define SPIF_CMD_CLEAR_FLAGS        0x50U   /* Micron   */
#define SPIF_CMD_ERASE_BLOCK32      0x52U
#define SPIF_CMD_READ_FLAGS         0x70U   /* Micron   */
#define SPIF_CMD_READ_OTP_ATMEL     0x77U   /* AT25DF   */
#define SPIF_CMD_SECURITY_ID        0x88U   /* SST26    */
#define SPIF_CMD_GLOBAL_UNLOCK      0x98U   /* SST26    */
#define SPIF_CMD_JEDEC_ID           0x9FU
#define SPIF_CMD_WAKE               0xABU
#define SPIF_CMD_ENTER_4BYTE        0xB7U
#define SPIF_CMD_SELECT_DIE         0xC2U   /* Winbond  */
#define SPIF_CMD_ERASE_CHIP         0xC7U
#define SPIF_CMD_ERASE_BLOCK        0xD8U
#define SPIF_CMD_ERASE_BLOCK_4B     0xDCU   /* Spansion */

/*
 * ****************************************************************************************************
 * Private function prototypes
 * ****************************************************************************************************
*/

static spif_err_t spif_erase(spif_t *handle, uint8_t command, uint32_t number, uint32_t unit,
                             uint32_t timeout_ms);
static spif_err_t spif_erase_start(spif_t *handle);
static spif_err_t spif_page_start(spif_t *handle);
#if (SPIF_TRANSFER == SPIF_TRANSFER_DMA)
static spif_err_t spif_piece_start(spif_t *handle);
static spif_err_t spif_job_moved(spif_t *handle);
#endif
static spif_err_t spif_job_stored(spif_t *handle);
static void       spif_job_end(spif_t *handle, spif_err_t err);
static uint32_t   spif_size_bytes(const uint8_t *id);
static void       spif_pick_commands(spif_t *handle, const uint8_t *id, uint32_t size);
static spif_err_t spif_unlock(spif_t *handle);
static bool       spif_id_command(const spif_t *handle, uint8_t *command, uint32_t *address,
                                  uint8_t *address_bytes, uint8_t *dummy_bytes);
static bool       spif_winbond_id(const spif_t *handle);
static spif_err_t spif_range(const spif_t *handle, uint32_t address, size_t len);
static uint32_t   spif_chunk(uint32_t address, size_t left, uint32_t boundary);
static spif_err_t spif_simple(spif_t *handle, uint8_t command);
static spif_err_t spif_command(spif_t *handle, uint8_t command, uint32_t address,
                               uint8_t address_bytes, uint8_t dummy_bytes);
static spif_err_t spif_register(spif_t *handle, uint8_t command, uint8_t *value);
static spif_err_t spif_transfer(spif_t *handle, uint8_t *data, size_t len, bool receive);
static spif_err_t spif_wait_ready(spif_t *handle, uint32_t timeout_ms);
static spif_err_t spif_busy(spif_t *handle, bool *busy);
static spif_err_t spif_refused(spif_t *handle, uint8_t clear);
static uint32_t   spif_dice(const spif_t *handle);
static spif_err_t spif_hal_error(HAL_StatusTypeDef status);
static uint32_t   spif_remaining(uint32_t start, uint32_t timeout_ms);
static void       spif_select(const spif_t *handle, bool select);

/*
 * ****************************************************************************************************
 * Public function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Set up a handle for one chip, find the chip and read its size.
 *
 * @param[out] handle   Handle to set up.
 * @param[in]  hspi     SPI bus the chip is on.
 * @param[in]  cs_port  Chip select port.
 * @param[in]  cs_pin   Chip select pin.
 * @return SPIF_ERR_NONE, SPIF_ERR_PROTECTED, SPIF_ERR_CHIP, SPIF_ERR_SPI,
 *         SPIF_ERR_TIMEOUT or SPIF_ERR_MUTEX. With SPIF_ERR_PROTECTED the handle
 *         works, but write protection is on, so the protected part of the chip
 *         cannot be changed. With SPIF_ERR_CHIP, manufacturer and memory_type
 *         hold what the chip sent, which says which chip it is.
 */
spif_err_t spif_init(spif_t *handle, SPI_HandleTypeDef *hspi, GPIO_TypeDef *cs_port,
                     uint16_t cs_pin)
{
    spif_err_t err                = SPIF_ERR_CHIP;
    spif_err_t protection         = SPIF_ERR_NONE;
    uint8_t    id[SPIF_ID_BYTES]  = { 0U, 0U, 0U, 0U, 0U, 0U };
    uint32_t   size               = 0U;

    assert_param(handle != NULL);
    assert_param(hspi != NULL);
    assert_param(cs_port != NULL);

    /* Runs once. A step that fails breaks out to the one return at the end. */
    do
    {
        /* A size of 0 marks the handle as not set up, and every other call
           refuses it. It stays 0 until every step below has worked. */
        handle->size    = 0U;
        handle->hspi    = hspi;
        handle->cs_port = cs_port;
        handle->cs_pin  = cs_pin;

        /* No job runs, and spif_wait() has nothing to report. */
        handle->job_state  = SPIF_JOB_IDLE;
        handle->job_result = SPIF_ERR_NONE;

        /* Deselected from the start, so the chip ignores anything sent on the
           bus to another device. */
        spif_select(handle, false);

        /* Straight after reset, give the chip the time it needs to power up. */
        while (HAL_GetTick() < SPIF_POWER_UP_MS)
        {
            osal_delay_ms(SPIF_POLL_MS);
        }

        /* Wake a chip an earlier run left in deep power down. One that is
           awake ignores it. It needs a few microseconds before the next. */
        err = spif_simple(handle, SPIF_CMD_WAKE);

        if (err != SPIF_ERR_NONE)
        {
            break;
        }

        osal_delay_ms(SPIF_POLL_MS);

        spif_select(handle, true);
        err = spif_command(handle, SPIF_CMD_JEDEC_ID, 0U, 0U, 0U);

        if (err == SPIF_ERR_NONE)
        {
            err = spif_transfer(handle, id, sizeof(id), true);
        }

        spif_select(handle, false);

        if (err != SPIF_ERR_NONE)
        {
            break;
        }

        /* Kept even for a chip refused below, so the caller can see what
           answered: 0xFF for no chip at all. */
        handle->manufacturer = id[0];
        handle->memory_type  = id[1];

        size = spif_size_bytes(id);

        if (size == 0U)
        {
            err = SPIF_ERR_CHIP;
            break;
        }

        spif_pick_commands(handle, id, size);

        /* Above 16 MB every maker but Spansion is switched to 4 byte addresses,
           and the usual commands then take four. The dedicated 4 byte commands
           are missing on the W25Q256FV, which the switch avoids. Micron wants
           a write enable before it, and the others do not mind one. */
        if ((handle->address_bytes == 4U) && (handle->manufacturer != SPIF_MANUFACTURER_SPANSION))
        {
            err = spif_simple(handle, SPIF_CMD_WRITE_ENABLE);

            if (err == SPIF_ERR_NONE)
            {
                err = spif_simple(handle, SPIF_CMD_ENTER_4BYTE);
            }
        }

        if (err != SPIF_ERR_NONE)
        {
            break;
        }

        /* Unlock a chip that locks itself at every power up, and see whether
           write protection is on. That does not stop the handle working:
           reads do, and so do writes outside the protected part. */
        protection = spif_unlock(handle);

        if ((protection != SPIF_ERR_NONE) && (protection != SPIF_ERR_PROTECTED))
        {
            err = protection;
            break;
        }

        /* The mutex comes last, so a chip that was not found leaves no mutex
           behind. Without an RTOS osal makes nothing, and with one it most
           often fails for a heap that is too small. */
        if (osal_mutex_create(&handle->mutex) != OSAL_ERR_NONE)
        {
            err = SPIF_ERR_MUTEX;
            break;
        }

        /* Only now may the other calls use it. */
        handle->size = size;
        err          = protection;
    }
    while (false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Read len bytes starting at address. Not from an interrupt.
 *
 * By polling it returns with the data read. With SPIF_TRANSFER_DMA it is a job:
 * it returns once the read has started, and spif_is_busy() or spif_wait() says
 * when it has finished. Until then the data is not there yet, and the buffer
 * must stay where it is.
 *
 * @param[in,out] handle   Handle from spif_init().
 * @param[in]     address  First byte to read.
 * @param[out]    data     Where the bytes go. With DMA, in memory the DMA can reach.
 * @param[in]     len      Bytes to read, up to the end of the chip.
 * @return SPIF_ERR_NONE, or the SPIF_ERR_ value that says what went wrong.
 */
spif_err_t spif_read(spif_t *handle, uint32_t address, uint8_t *data, size_t len)
{
    spif_err_t err  = SPIF_ERR_INVALID;
    osal_err_t lock = OSAL_ERR_NONE;

    assert_param(handle != NULL);
    assert_param(data != NULL);

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* A handle spif_init() did not accept has a size of 0. A NULL pointer
           is the caller's bug, left to assert_param above. */
        if (handle->size == 0U)
        {
            break;
        }

        /* One job at a time. spif_is_busy() says when the last has ended. */
        if (handle->job_state != SPIF_JOB_IDLE)
        {
            err = SPIF_ERR_BUSY;
            break;
        }

        /* Nothing goes to the bus for a range past the end of the chip, and
           nothing at all for 0 bytes, which succeeds. */
        err = spif_range(handle, address, len);

        if ((err != SPIF_ERR_NONE) || (len == 0U))
        {
            break;
        }

        /* One thread at a time. Without an RTOS osal takes nothing and this
           always succeeds. */
        lock = osal_mutex_lock(&handle->mutex, SPIF_TIMEOUT_MUTEX_MS);

        if (lock == OSAL_ERR_TIMEOUT)
        {
            /* Another thread kept the chip for the whole wait. */
            err = SPIF_ERR_TIMEOUT;
            break;
        }

        if (lock != OSAL_ERR_NONE)
        {
            /* Refused outright, such as from an interrupt. */
            err = SPIF_ERR_MUTEX;
            break;
        }

        /* No break from here on: the mutex is held and the chip selected, and
           both are given back below. A fast read, which every chip takes at
           any clock, where the plain read stops at 50 MHz on some. After the
           address and one dummy byte the chip sends from there on, to the end
           of the chip, however many pieces the HAL takes it in. */
        spif_select(handle, true);
        err = spif_command(handle, handle->cmd_read, address, handle->address_bytes, 1U);

#if (SPIF_TRANSFER == SPIF_TRANSFER_DMA)
        /* The DMA brings the data in from here, as a job. The chip stays
           selected and the mutex held until spif_is_busy() sees the last of
           it arrive. */
        if (err == SPIF_ERR_NONE)
        {
            handle->job_state = SPIF_JOB_READ;
            handle->job_data  = data;
            handle->job_len   = len;
            handle->job_done  = 0U;
            err               = spif_piece_start(handle);
        }

        if (err != SPIF_ERR_NONE)
        {
            spif_job_end(handle, err);
        }
#else
        if (err == SPIF_ERR_NONE)
        {
            err = spif_transfer(handle, data, len, true);
        }

        spif_select(handle, false);
        osal_mutex_unlock(&handle->mutex);
#endif
    }
    while (false);

    /* What spif_wait() reports. A job started here, or one still running,
       puts its own result over this when it ends. */
    handle->job_result = err;

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Start writing len bytes at address, into flash erased beforehand. Not from an interrupt.
 *
 * A job: it returns once the first page has started, and spif_is_busy() sends
 * each of the others once the chip has stored the one before. spif_is_busy()
 * or spif_wait() says when the last is stored. Until then the buffer must stay
 * as it is.
 *
 * @param[in,out] handle   Handle from spif_init().
 * @param[in]     address  First byte to write.
 * @param[in]     data     The bytes to write. With DMA, in memory the DMA can reach.
 * @param[in]     len      Bytes to write, up to the end of the chip.
 * @return SPIF_ERR_NONE once started, or the SPIF_ERR_ value that says what
 *         went wrong. After an error, the pages before the failing one are written.
 */
spif_err_t spif_write(spif_t *handle, uint32_t address, const uint8_t *data, size_t len)
{
    spif_err_t err  = SPIF_ERR_INVALID;
    osal_err_t lock = OSAL_ERR_NONE;

    assert_param(handle != NULL);
    assert_param(data != NULL);

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* A handle spif_init() did not accept has a size of 0. A NULL pointer
           is the caller's bug, left to assert_param above. */
        if (handle->size == 0U)
        {
            break;
        }

        /* One job at a time. spif_is_busy() says when the last has ended. */
        if (handle->job_state != SPIF_JOB_IDLE)
        {
            err = SPIF_ERR_BUSY;
            break;
        }

        /* Nothing goes to the bus for a range past the end of the chip, and
           nothing at all for 0 bytes, which succeeds. */
        err = spif_range(handle, address, len);

        if ((err != SPIF_ERR_NONE) || (len == 0U))
        {
            break;
        }

        /* One thread at a time. Without an RTOS osal takes nothing and this
           always succeeds. */
        lock = osal_mutex_lock(&handle->mutex, SPIF_TIMEOUT_MUTEX_MS);

        if (lock == OSAL_ERR_TIMEOUT)
        {
            /* Another thread kept the chip for the whole wait. */
            err = SPIF_ERR_TIMEOUT;
            break;
        }

        if (lock != OSAL_ERR_NONE)
        {
            /* Refused outright, such as from an interrupt. */
            err = SPIF_ERR_MUTEX;
            break;
        }

        /* No break from here on: the mutex is held until the job ends. A page
           at a time: sent more than a page, the chip wraps round inside it and
           overwrites what it has just been given. The HAL takes a pointer to
           non-const, but sending only reads through it, so casting the const
           away is safe. */
        handle->job_data    = (uint8_t *)data;
        handle->job_len     = len;
        handle->job_done    = 0U;
        handle->job_address = address;
        err                 = spif_page_start(handle);

        if (err != SPIF_ERR_NONE)
        {
            spif_job_end(handle, err);
        }
    }
    while (false);

    /* What spif_wait() reports. A job started here, or one still running,
       puts its own result over this when it ends. */
    handle->job_result = err;

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Start erasing one sector, the smallest part the chip erases, by its number.
 *
 * A job: it returns at once, and spif_is_busy() or spif_wait() says when it
 * has finished. Not from an interrupt.
 *
 * @param[in,out] handle  Handle from spif_init().
 * @param[in]     sector  Sector number, SPIF_ADDRESS_TO_SECTOR() of an address in it.
 * @return SPIF_ERR_NONE once started, or the SPIF_ERR_ value that says what went wrong.
 */
spif_err_t spif_erase_sector(spif_t *handle, uint32_t sector)
{
    assert_param(handle != NULL);

    return spif_erase(handle, handle->cmd_erase_sector, sector, handle->sector_size,
                      SPIF_TIMEOUT_SECTOR_MS);
}

/*****************************************************************************************************/
/**
 * @brief Start erasing one 32 KB block, by its number. A job, as spif_erase_sector().
 *
 * @param[in,out] handle   Handle from spif_init().
 * @param[in]     block32  Block number, SPIF_ADDRESS_TO_BLOCK32() of an address in it.
 * @return SPIF_ERR_NONE once started, or the SPIF_ERR_ value that says what went
 *         wrong. SPIF_ERR_INVALID on a chip whose sectors are bigger than 32 KB.
 */
spif_err_t spif_erase_block32(spif_t *handle, uint32_t block32)
{
    assert_param(handle != NULL);

    return spif_erase(handle, handle->cmd_erase_block32, block32, SPIF_BLOCK32_SIZE,
                      SPIF_TIMEOUT_BLOCK32_MS);
}

/*****************************************************************************************************/
/**
 * @brief Start erasing one 64 KB block, by its number. A job, as spif_erase_sector().
 *
 * @param[in,out] handle  Handle from spif_init().
 * @param[in]     block   Block number, SPIF_ADDRESS_TO_BLOCK() of an address in it.
 * @return SPIF_ERR_NONE once started, or the SPIF_ERR_ value that says what went
 *         wrong. SPIF_ERR_INVALID on a chip whose sectors are bigger than 64 KB.
 */
spif_err_t spif_erase_block(spif_t *handle, uint32_t block)
{
    assert_param(handle != NULL);

    return spif_erase(handle, handle->cmd_erase_block, block, SPIF_BLOCK_SIZE,
                      SPIF_TIMEOUT_BLOCK_MS);
}

/*****************************************************************************************************/
/**
 * @brief Start erasing the whole chip. A job, as spif_erase_sector().
 *
 * @param[in,out] handle  Handle from spif_init().
 * @return SPIF_ERR_NONE once started, or the SPIF_ERR_ value that says what went wrong.
 */
spif_err_t spif_erase_chip(spif_t *handle)
{
    /* The time allowed grows with the chip, a MB at a time, rounded up. */
    uint32_t megabytes = 0U;

    assert_param(handle != NULL);

    megabytes = (handle->size + 0xFFFFFU) >> 20U;

    return spif_erase(handle, SPIF_CMD_ERASE_CHIP, 0U, 0U,
                      megabytes * SPIF_TIMEOUT_CHIP_PER_MB_MS);
}

/*****************************************************************************************************/
/**
 * @brief Read the chip's 64 bit unique ID, set at the factory. Not from an interrupt.
 *
 * Each maker gives it its own way. Winbond, GigaDevice, ISSI, Puya, XMC, Zbit,
 * Boya, Adesto AT25SF and AT25DF, Spansion and SST26 are known. Macronix and
 * Micron have no such ID, and other makers are not known.
 *
 * @param[in,out] handle  Handle from spif_init().
 * @param[out]    id      SPIF_UNIQUE_ID_SIZE bytes for the ID.
 * @return SPIF_ERR_NONE, or the SPIF_ERR_ value that says what went wrong.
 *         SPIF_ERR_INVALID for a chip without a known ID.
 */
spif_err_t spif_unique_id(spif_t *handle, uint8_t *id)
{
    spif_err_t err           = SPIF_ERR_INVALID;
    osal_err_t lock          = OSAL_ERR_NONE;
    uint8_t    command       = 0U;
    uint32_t   address       = 0U;
    uint8_t    address_bytes = 0U;
    uint8_t    dummy_bytes   = 0U;

    assert_param(handle != NULL);
    assert_param(id != NULL);

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* A handle spif_init() did not accept has a size of 0, and a chip
           without a known ID is refused before anything is sent. */
        if ((handle->size == 0U) ||
            !spif_id_command(handle, &command, &address, &address_bytes, &dummy_bytes))
        {
            break;
        }

        /* One job at a time. spif_is_busy() says when the last has ended. */
        if (handle->job_state != SPIF_JOB_IDLE)
        {
            err = SPIF_ERR_BUSY;
            break;
        }

        /* One thread at a time. Without an RTOS osal takes nothing and this
           always succeeds. */
        lock = osal_mutex_lock(&handle->mutex, SPIF_TIMEOUT_MUTEX_MS);

        if (lock == OSAL_ERR_TIMEOUT)
        {
            /* Another thread kept the chip for the whole wait. */
            err = SPIF_ERR_TIMEOUT;
            break;
        }

        if (lock != OSAL_ERR_NONE)
        {
            /* Refused outright, such as from an interrupt. */
            err = SPIF_ERR_MUTEX;
            break;
        }

        /* No break from here on: the mutex is held, and is given back below. */
        spif_select(handle, true);
        err = spif_command(handle, command, address, address_bytes, dummy_bytes);

        if (err == SPIF_ERR_NONE)
        {
            err = spif_transfer(handle, id, SPIF_UNIQUE_ID_SIZE, true);
        }

        spif_select(handle, false);
        osal_mutex_unlock(&handle->mutex);
    }
    while (false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Whether a job is still running. Moves it on, and returns at once. Not from an interrupt.
 *
 * A job is a write, an erase, or with DMA a read. Each call checks once: it
 * asks the chip, or the HAL with DMA, whether the piece under way has
 * finished, and if it has, starts the next page or the next part of an erase.
 * Call it from the thread that started the job, which holds the mutex until
 * the job ends. Once it returns false, spif_wait() says how the job ended.
 *
 * @param[in,out] handle  Handle from spif_init().
 * @return true while the job runs, false once it has ended or when none runs.
 */
bool spif_is_busy(spif_t *handle)
{
    spif_err_t err = SPIF_ERR_NONE;

    assert_param(handle != NULL);

    if ((handle->job_state == SPIF_JOB_PROGRAM) || (handle->job_state == SPIF_JOB_ERASE))
    {
        /* The chip is storing a page or erasing: ask it once whether it has
           finished. */
        err = spif_job_stored(handle);
    }
#if (SPIF_TRANSFER == SPIF_TRANSFER_DMA)
    else if (handle->job_state != SPIF_JOB_IDLE)
    {
        /* Data is moving: ask the HAL once whether the DMA has finished. */
        err = spif_job_moved(handle);
    }
#endif
    else
    {
        /* Nothing runs. */
    }

    /* Ended, well or not: the chip and the mutex are given back, and how it
       ended is kept for spif_wait(). */
    if ((err != SPIF_ERR_BUSY) && (handle->job_state != SPIF_JOB_IDLE))
    {
        spif_job_end(handle, err);
    }

    return (err == SPIF_ERR_BUSY);
}

/*****************************************************************************************************/
/**
 * @brief Wait until the running job has ended, and say how it ended. Not from an interrupt.
 *
 * Asks first, so after spif_is_busy() has returned false, or after a call that
 * finished or was refused at once, it returns straight away. Between the
 * questions it sleeps a millisecond through osal, so with an RTOS other
 * threads run. Call it from the thread that started the job.
 *
 * @param[in,out] handle  Handle from spif_init().
 * @return How the last read, write or erase ended: SPIF_ERR_NONE when it worked.
 */
spif_err_t spif_wait(spif_t *handle)
{
    assert_param(handle != NULL);

    while (spif_is_busy(handle))
    {
        osal_delay_ms(SPIF_POLL_MS);
    }

    return handle->job_result;
}

/*
 * ****************************************************************************************************
 * Private function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Start erasing one sector or block, or the whole chip, as a job.
 *
 * @param[in,out] handle      Handle from spif_init().
 * @param[in]     command     The erase command, 0 when the chip has none for this size.
 * @param[in]     number      Which sector or block.
 * @param[in]     unit        Bytes one number covers, or 0 for the whole chip.
 * @param[in]     timeout_ms  Time allowed for one erase of this size.
 * @return SPIF_ERR_NONE once started, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_erase(spif_t *handle, uint8_t command, uint32_t number, uint32_t unit,
                             uint32_t timeout_ms)
{
    spif_err_t err   = SPIF_ERR_INVALID;
    osal_err_t lock  = OSAL_ERR_NONE;
    uint32_t   step  = unit;
    uint32_t   count = 1U;
    uint32_t   wait  = timeout_ms;

    /* Runs once. A check that fails breaks out to the one return at the end. */
    do
    {
        /* A handle spif_init() did not accept has a size of 0. */
        if (handle->size == 0U)
        {
            break;
        }

        /* One job at a time. spif_is_busy() says when the last has ended. */
        if (handle->job_state != SPIF_JOB_IDLE)
        {
            err = SPIF_ERR_BUSY;
            break;
        }

        if ((unit == 0U) && (handle->size > SPIF_DIE_SIZE))
        {
            /* A chip over 64 MB is two or four dies, and the makers differ in
               how a whole one is erased. So it goes a block at a time, or a
               sector at a time on a chip with no 64 KB erase. */
            command = (handle->cmd_erase_block != 0U) ? handle->cmd_erase_block
                                                      : handle->cmd_erase_sector;
            step    = (handle->cmd_erase_block != 0U) ? SPIF_BLOCK_SIZE : handle->sector_size;
            wait    = (handle->cmd_erase_block != 0U) ? SPIF_TIMEOUT_BLOCK_MS
                                                      : SPIF_TIMEOUT_SECTOR_MS;
            count   = handle->size / step;
        }
        else if (command == 0U)
        {
            /* No command for this size. A size made of smaller sectors goes a
               sector at a time. One smaller than a sector cannot be erased. */
            if (handle->sector_size >= unit)
            {
                break;
            }

            command = handle->cmd_erase_sector;
            step    = handle->sector_size;
            wait    = SPIF_TIMEOUT_SECTOR_MS;
            count   = unit / handle->sector_size;
        }
        else
        {
            /* The chip has a command for exactly this. */
        }

        /* A number past the last one is refused before anything is sent. */
        if ((unit != 0U) && (number >= (handle->size / unit)))
        {
            err = SPIF_ERR_RANGE;
            break;
        }

        /* One thread at a time. Without an RTOS osal takes nothing and this
           always succeeds. */
        lock = osal_mutex_lock(&handle->mutex, SPIF_TIMEOUT_MUTEX_MS);

        if (lock == OSAL_ERR_TIMEOUT)
        {
            /* Another thread kept the chip for the whole wait. */
            err = SPIF_ERR_TIMEOUT;
            break;
        }

        if (lock != OSAL_ERR_NONE)
        {
            /* Refused outright, such as from an interrupt. */
            err = SPIF_ERR_MUTEX;
            break;
        }

        /* No break from here on: the mutex is held until the job ends. The
           first erase goes now, and spif_is_busy() sends each of the others
           once the chip has done the one before. */
        handle->job_command = command;
        handle->job_address = number * unit;
        handle->job_step    = step;
        handle->job_len     = count;
        handle->job_done    = 0U;
        handle->job_timeout = wait;
        err                 = spif_erase_start(handle);

        if (err != SPIF_ERR_NONE)
        {
            spif_job_end(handle, err);
        }
    }
    while (false);

    /* What spif_wait() reports. A job started here, or one still running,
       puts its own result over this when it ends. */
    handle->job_result = err;

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Send the next erase of an erase job.
 *
 * @param[in,out] handle  Handle of the chip, with an erase job under way.
 * @return SPIF_ERR_NONE once sent, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_erase_start(spif_t *handle)
{
    uint32_t   address = handle->job_address + ((uint32_t)handle->job_done * handle->job_step);
    spif_err_t err     = SPIF_ERR_NONE;

    handle->job_state = SPIF_JOB_ERASE;

    /* The chip erases only after a write enable, and clears that itself when
       it is done. */
    err = spif_simple(handle, SPIF_CMD_WRITE_ENABLE);

    if (err == SPIF_ERR_NONE)
    {
        spif_select(handle, true);
        err = spif_command(handle, handle->job_command, address,
                           (handle->job_command == SPIF_CMD_ERASE_CHIP) ? 0U
                                                                        : handle->address_bytes,
                           0U);
        spif_select(handle, false);
    }

    /* The erase starts when chip select goes high, and its time with it. */
    handle->job_start = HAL_GetTick();

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Send the next page of a write job: its command by polling, its data by polling or DMA.
 *
 * @param[in,out] handle  Handle of the chip, with a write job under way.
 * @return SPIF_ERR_NONE once sent, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_page_start(spif_t *handle)
{
    uint32_t   at  = handle->job_address + (uint32_t)handle->job_done;
    spif_err_t err = SPIF_ERR_NONE;

    /* From here to the end of the page, or less when that is all there is. */
    handle->job_step = spif_chunk(at, handle->job_len - handle->job_done, SPIF_PAGE_SIZE);

    /* Every page needs a write enable of its own: the chip clears it when the
       page is done. */
    err = spif_simple(handle, SPIF_CMD_WRITE_ENABLE);

    if (err == SPIF_ERR_NONE)
    {
        spif_select(handle, true);
        err = spif_command(handle, handle->cmd_program, at, handle->address_bytes, 0U);
    }

#if (SPIF_TRANSFER == SPIF_TRANSFER_DMA)
    /* The DMA sends the data, and spif_is_busy() lets the chip go once it has. */
    handle->job_state   = SPIF_JOB_SEND;
    handle->job_start   = HAL_GetTick();
    handle->job_timeout = SPIF_TIMEOUT_TRANSFER_MS;

    if (err == SPIF_ERR_NONE)
    {
        err = spif_hal_error(HAL_SPI_Transmit_DMA(handle->hspi,
                                                  &handle->job_data[handle->job_done],
                                                  (uint16_t)handle->job_step));
    }
#else
    if (err == SPIF_ERR_NONE)
    {
        err = spif_transfer(handle, &handle->job_data[handle->job_done], handle->job_step, false);
    }

    /* The chip stores the page once chip select goes high, and its time
       starts then. */
    spif_select(handle, false);
    handle->job_state   = SPIF_JOB_PROGRAM;
    handle->job_start   = HAL_GetTick();
    handle->job_timeout = SPIF_TIMEOUT_PAGE_MS;
#endif

    return err;
}

#if (SPIF_TRANSFER == SPIF_TRANSFER_DMA)

/*****************************************************************************************************/
/**
 * @brief Start the DMA on the next piece of a read job.
 *
 * @param[in,out] handle  Handle of the chip, with a read job under way.
 * @return SPIF_ERR_NONE once started, or SPIF_ERR_SPI.
 */
static spif_err_t spif_piece_start(spif_t *handle)
{
    /* The HAL counts in 16 bits, so a long read goes in pieces. */
    size_t chunk = handle->job_len - handle->job_done;

    if (chunk > SPIF_TRANSFER_LIMIT)
    {
        chunk = SPIF_TRANSFER_LIMIT;
    }

    handle->job_step    = (uint32_t)chunk;
    handle->job_start   = HAL_GetTick();
    handle->job_timeout = SPIF_TIMEOUT_TRANSFER_MS;

    return spif_hal_error(HAL_SPI_Receive_DMA(handle->hspi, &handle->job_data[handle->job_done],
                                              (uint16_t)chunk));
}

/*****************************************************************************************************/
/**
 * @brief Ask the HAL once whether the DMA has finished, and go on when it has.
 *
 * @param[in,out] handle  Handle of the chip, with data moving.
 * @return SPIF_ERR_BUSY while the job runs, SPIF_ERR_NONE when a read is done,
 *         or what went wrong.
 */
static spif_err_t spif_job_moved(spif_t *handle)
{
    spif_err_t err = SPIF_ERR_BUSY;

    if (HAL_SPI_GetState(handle->hspi) != HAL_SPI_STATE_READY)
    {
        if (spif_remaining(handle->job_start, handle->job_timeout) == 0U)
        {
            /* Stuck. Stopped, or the next transfer finds the SPI still busy. */
            (void)HAL_SPI_Abort(handle->hspi);
            err = SPIF_ERR_TIMEOUT;
        }
    }
    else if (HAL_SPI_GetError(handle->hspi) != HAL_SPI_ERROR_NONE)
    {
        /* Finished is not the same as finished well. */
        err = SPIF_ERR_SPI;
    }
    else if (handle->job_state == SPIF_JOB_SEND)
    {
        /* The page is out. The chip stores it once it is let go. */
        spif_select(handle, false);
        handle->job_state   = SPIF_JOB_PROGRAM;
        handle->job_start   = HAL_GetTick();
        handle->job_timeout = SPIF_TIMEOUT_PAGE_MS;
    }
    else
    {
        /* A piece of a read is in. The chip goes on sending from where it
           stopped, so the next piece needs no new command. */
        handle->job_done += handle->job_step;

        if (handle->job_done < handle->job_len)
        {
            err = spif_piece_start(handle);
            err = (err == SPIF_ERR_NONE) ? SPIF_ERR_BUSY : err;
        }
        else
        {
            err = SPIF_ERR_NONE;
        }
    }

    return err;
}

#endif /* SPIF_TRANSFER == SPIF_TRANSFER_DMA */

/*****************************************************************************************************/
/**
 * @brief Ask the chip once whether it has stored the page or done the erase, and go on if so.
 *
 * @param[in,out] handle  Handle of the chip, storing a page or erasing.
 * @return SPIF_ERR_BUSY while the job runs, SPIF_ERR_NONE when it is done, or
 *         what went wrong.
 */
static spif_err_t spif_job_stored(spif_t *handle)
{
    bool       busy    = false;
    bool       program = (handle->job_state == SPIF_JOB_PROGRAM);
    spif_err_t err     = spif_busy(handle, &busy);

    if ((err == SPIF_ERR_NONE) && busy)
    {
        err = (spif_remaining(handle->job_start, handle->job_timeout) == 0U) ? SPIF_ERR_TIMEOUT
                                                                             : SPIF_ERR_BUSY;
    }
    else if (err == SPIF_ERR_NONE)
    {
        /* Done: on to the next page or erase, if there is one. */
        handle->job_done += program ? handle->job_step : 1U;

        if (handle->job_done < handle->job_len)
        {
            err = program ? spif_page_start(handle) : spif_erase_start(handle);
            err = (err == SPIF_ERR_NONE) ? SPIF_ERR_BUSY : err;
        }
    }
    else
    {
        /* The chip refused, or the status could not be read. */
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief End a job: let the chip go, give the mutex back, and keep how it ended.
 *
 * @param[in,out] handle  Handle of the chip.
 * @param[in]     err     How the job ended.
 */
static void spif_job_end(spif_t *handle, spif_err_t err)
{
    handle->job_state  = SPIF_JOB_IDLE;
    handle->job_result = err;
    spif_select(handle, false);
    osal_mutex_unlock(&handle->mutex);
}

/*****************************************************************************************************/
/**
 * @brief Size of the chip in bytes from its JEDEC ID, or 0 for one not known.
 *
 * @param[in] id  The JEDEC ID: maker, memory type, capacity.
 * @return Bytes, or 0.
 */
static uint32_t spif_size_bytes(const uint8_t *id)
{
    uint32_t bytes    = 0U;
    uint8_t  type     = id[1];
    uint8_t  capacity = id[2];
    uint8_t  density  = (uint8_t)(type & 0x1FU);

    if (id[0] == SPIF_MANUFACTURER_SST)
    {
        /* SST26 counts in a way of its own. SST25 programs a byte at a time
           rather than a page, which this driver does not do, so it stays 0. */
        if ((type == 0x26U) && ((capacity == 0x41U) || (capacity == 0x51U)))
        {
            bytes = 0x200000U;
        }
        else if ((type == 0x26U) && (capacity == 0x42U))
        {
            bytes = 0x400000U;
        }
        else if ((type == 0x26U) && (capacity == 0x43U))
        {
            bytes = 0x800000U;
        }
        else
        {
            /* SST25, or an SST26 not known. */
        }
    }
    else if (id[0] == SPIF_MANUFACTURER_SEMPER)
    {
        /* Infineon Semper erases 256 KB at a time and stays busy after any
           refused command, so it is refused here rather than half handled. */
    }
    else if ((capacity >= 0x11U) && (capacity <= 0x1CU))
    {
        /* A power of two in bytes: 0x11 is 128 KB, a 1 Mbit chip, 0x19 is
           32 MB, and Macronix goes on to 0x1C for 256 MB. */
        bytes = (uint32_t)1U << capacity;
    }
    else if ((capacity >= 0x20U) && (capacity <= 0x22U))
    {
        /* Winbond, Micron and others jump from 0x19 to 0x20 for 64 MB, then
           0x21 for 128 MB and 0x22 for 256 MB. */
        bytes = SPIF_DIE_SIZE << (capacity - 0x20U);
    }
    else if ((id[0] == SPIF_MANUFACTURER_MACRONIX) && (capacity >= 0x32U) && (capacity <= 0x3CU))
    {
        /* Macronix 1.8 V chips count from 0x32 for 256 KB. */
        bytes = (uint32_t)1U << (capacity - 0x20U);
    }
    else if ((id[0] == SPIF_MANUFACTURER_ATMEL) && (((type & 0xE0U) == 0x40U) ||
                                                    ((type & 0xE0U) == 0x80U)) &&
             (density >= 1U) && (density <= 9U))
    {
        /* Adesto AT25DF, AT25FF and AT25SF give a family in the top three bits
           of the memory type and the size below them: 4 is 512 KB, 7 is 4 MB.
           Their DataFlash, AT45, works differently and stays 0. */
        bytes = (uint32_t)1U << (density + 15U);
    }
    else
    {
        /* Not a size this driver knows, or no chip at all. */
    }

    return bytes;
}

/*****************************************************************************************************/
/**
 * @brief Choose the family, sector size, address length and commands for this chip.
 *
 * @param[in,out] handle  Handle being set up, its manufacturer and memory type read.
 * @param[in]     id      The JEDEC ID, all six bytes.
 * @param[in]     size    Bytes in the chip.
 */
static void spif_pick_commands(spif_t *handle, const uint8_t *id, uint32_t size)
{
    uint8_t maker = handle->manufacturer;
    uint8_t type  = handle->memory_type;

    /* What nearly every chip takes: 4 KB sectors, and 3 address bytes up to
       16 MB, 4 above it. */
    handle->family            = SPIF_FAMILY_STANDARD;
    handle->sector_size       = SPIF_SECTOR_4K;
    handle->address_bytes     = (size > SPIF_3BYTE_LIMIT) ? 4U : 3U;
    handle->cmd_read          = SPIF_CMD_FAST_READ;
    handle->cmd_program       = SPIF_CMD_PROGRAM;
    handle->cmd_erase_sector  = SPIF_CMD_ERASE_SECTOR;
    handle->cmd_erase_block32 = SPIF_CMD_ERASE_BLOCK32;
    handle->cmd_erase_block   = SPIF_CMD_ERASE_BLOCK;

    if ((maker == SPIF_MANUFACTURER_MICRON) && ((type == 0xBAU) || (type == 0xBBU)))
    {
        /* Micron N25Q and MT25Q report busy, and a refused command, in a flag
           status register that also covers every die of a stacked chip. */
        handle->family = SPIF_FAMILY_MICRON;
    }
    else if ((maker == SPIF_MANUFACTURER_MICRON) && (type == 0x20U))
    {
        /* The old ST M25P has no 4 KB erase: its sectors are 64 KB, 32 KB on
           the M25P10 and 256 KB on the M25P128. */
        handle->sector_size = (id[2] == 0x11U) ? SPIF_SECTOR_32K
                            : ((id[2] == 0x18U) ? SPIF_SECTOR_256K : SPIF_SECTOR_64K);
    }
    else if ((maker == SPIF_MANUFACTURER_SPANSION) && ((type == 0x02U) || (type == 0x20U)))
    {
        /* Spansion FL-S, FL-P and SL have 64 KB or 256 KB sectors, which the
           fourth and fifth extra ID bytes tell apart. Their 4 KB parameter
           sectors cover only 128 KB of the chip, so they are not used. */
        handle->family      = SPIF_FAMILY_SPANSION;
        handle->sector_size = ((id[4] == 0x00U) && ((id[3] == 0x4DU) || (id[3] == 0x03U)) &&
                               (size >= SPIF_3BYTE_LIMIT)) ? SPIF_SECTOR_256K : SPIF_SECTOR_64K;
    }
    else if (maker == SPIF_MANUFACTURER_SST)
    {
        /* Only SST26 gets this far. It has no 32 KB erase, and its 64 KB erase
           clears 8 KB or 32 KB near the two ends, so both go a sector at a time. */
        handle->family            = SPIF_FAMILY_SST26;
        handle->cmd_erase_block32 = 0U;
        handle->cmd_erase_block   = 0U;
    }
    else if (((maker == SPIF_MANUFACTURER_ATMEL) && ((type & 0xE0U) == 0x40U) && (id[2] < 0x11U)) ||
             ((maker == SPIF_MANUFACTURER_ESMT) && (type == 0x20U)))
    {
        /* AT25DF, AT25FF and the ESMT F25L lock themselves at power up. The
           AT25SL321 shares the AT25DF's family bits, but gives its size the
           usual way and works like a Winbond. */
        handle->family = SPIF_FAMILY_UNPROTECT;
    }
    else
    {
        /* Everything like Winbond. */
    }

    if (handle->sector_size != SPIF_SECTOR_4K)
    {
        /* The 64 KB erase command erases one of these big sectors, whatever
           its size, and is the 32 KB or 64 KB erase only when that is the size. */
        handle->cmd_erase_sector  = SPIF_CMD_ERASE_BLOCK;
        handle->cmd_erase_block32 = (handle->sector_size == SPIF_SECTOR_32K) ? SPIF_CMD_ERASE_BLOCK
                                                                             : 0U;
        handle->cmd_erase_block   = (handle->sector_size == SPIF_SECTOR_64K) ? SPIF_CMD_ERASE_BLOCK
                                                                             : 0U;
    }

    if ((handle->address_bytes == 4U) && (maker == SPIF_MANUFACTURER_SPANSION))
    {
        /* Spansion has no 4 byte mode, but commands of its own that always
           take four bytes. It has no 32 KB erase among them. */
        handle->cmd_read          = SPIF_CMD_FAST_READ_4B;
        handle->cmd_program       = SPIF_CMD_PROGRAM_4B;
        handle->cmd_erase_sector  = (handle->sector_size == SPIF_SECTOR_4K)
                                        ? SPIF_CMD_ERASE_SECTOR_4B : SPIF_CMD_ERASE_BLOCK_4B;
        handle->cmd_erase_block32 = 0U;
        handle->cmd_erase_block   = (handle->cmd_erase_block != 0U) ? SPIF_CMD_ERASE_BLOCK_4B : 0U;
    }
}

/*****************************************************************************************************/
/**
 * @brief Unlock a chip that locks itself at power up, and say whether write protection is on.
 *
 * @param[in,out] handle  Handle being set up, its family chosen.
 * @return SPIF_ERR_NONE, SPIF_ERR_PROTECTED, or the SPIF_ERR_ value of a failed transfer.
 */
static spif_err_t spif_unlock(spif_t *handle)
{
    spif_err_t err   = SPIF_ERR_NONE;
    uint8_t    value = 0U;
    uint8_t    mask  = SPIF_STATUS_BP;
    uint8_t    i     = 0U;

    if (handle->family == SPIF_FAMILY_SST26)
    {
        /* Locked at every power up. One command unlocks the whole chip, all
           but blocks locked for ever, which the configuration register tells
           of. */
        err = spif_simple(handle, SPIF_CMD_WRITE_ENABLE);

        if (err == SPIF_ERR_NONE)
        {
            err = spif_simple(handle, SPIF_CMD_GLOBAL_UNLOCK);
        }

        if (err == SPIF_ERR_NONE)
        {
            err = spif_register(handle, SPIF_CMD_READ_CONFIG, &value);
        }

        if ((err == SPIF_ERR_NONE) && ((value & SPIF_CONFIG_BPNV) == 0U))
        {
            err = SPIF_ERR_PROTECTED;
        }
    }
    else
    {
        if (handle->family == SPIF_FAMILY_UNPROTECT)
        {
            /* Locked at every power up. Writing 00h to the status register
               unlocks it. Twice: on an AT25DF whose protection is itself
               locked (SPRL), the first only lifts that lock. */
            for (i = 0U; (err == SPIF_ERR_NONE) && (i < 2U); i++)
            {
                err = spif_simple(handle, SPIF_CMD_WRITE_ENABLE);

                if (err == SPIF_ERR_NONE)
                {
                    /* The 00h is the one zero byte sent after the command. */
                    spif_select(handle, true);
                    err = spif_command(handle, SPIF_CMD_WRITE_STATUS, 0U, 0U, 1U);
                    spif_select(handle, false);
                }

                if (err == SPIF_ERR_NONE)
                {
                    err = spif_wait_ready(handle, SPIF_TIMEOUT_STATUS_MS);
                }
            }

            /* An AT25DF reports its sectors in bits 2 and 3. Bit 4 there is the
               WP pin, high in normal use. */
            if (handle->manufacturer == SPIF_MANUFACTURER_ATMEL)
            {
                mask = SPIF_STATUS_SWP;
            }
        }

        /* BP0 to BP2 set protect part or all of the chip, on every maker. */
        if (err == SPIF_ERR_NONE)
        {
            err = spif_register(handle, SPIF_CMD_READ_STATUS, &value);
        }

        if ((err == SPIF_ERR_NONE) && ((value & mask) != 0U))
        {
            err = SPIF_ERR_PROTECTED;
        }
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Say how this chip gives its unique ID, if it does.
 *
 * @param[in]  handle         Handle of the chip.
 * @param[out] command        The command.
 * @param[out] address        The address it is sent with.
 * @param[out] address_bytes  How many address bytes.
 * @param[out] dummy_bytes    Dummy bytes after the address.
 * @return true when the chip has a known ID.
 */
static bool spif_id_command(const spif_t *handle, uint8_t *command, uint32_t *address,
                            uint8_t *address_bytes, uint8_t *dummy_bytes)
{
    bool known = true;

    /* Winbond's way, which most makers copy: a zero address and a dummy byte,
       so four zero bytes, five in 4 byte address mode, then the ID. */
    *command       = SPIF_CMD_UNIQUE_ID;
    *address       = 0U;
    *address_bytes = handle->address_bytes;
    *dummy_bytes   = 1U;

    if (handle->family == SPIF_FAMILY_SPANSION)
    {
        /* A random number set at the factory, at the start of the OTP area,
           read with three address bytes whatever the size of the chip. */
        *address_bytes = 3U;
    }
    else if (handle->family == SPIF_FAMILY_SST26)
    {
        /* The first eight bytes of the Security ID, set at the factory, after
           two address bytes and a dummy. */
        *command       = SPIF_CMD_SECURITY_ID;
        *address_bytes = 2U;
    }
    else if ((handle->family == SPIF_FAMILY_UNPROTECT) &&
             (handle->manufacturer == SPIF_MANUFACTURER_ATMEL))
    {
        /* The OTP security register of an AT25DF, whose second half, from
           byte 64, is set at the factory. Two dummy bytes. */
        *command       = SPIF_CMD_READ_OTP_ATMEL;
        *address       = 0x40U;
        *address_bytes = 3U;
        *dummy_bytes   = 2U;
    }
    else if (!spif_winbond_id(handle))
    {
        /* Macronix and Micron have none, and other makers are not known. */
        known = false;
    }
    else
    {
        /* Winbond's way, as set above. */
    }

    return known;
}

/*****************************************************************************************************/
/**
 * @brief Whether the chip gives its unique ID the way Winbond's do.
 *
 * @param[in] handle  Handle of the chip.
 * @return true for Winbond and the makers that copy it.
 */
static bool spif_winbond_id(const spif_t *handle)
{
    uint8_t maker = handle->manufacturer;
    uint8_t type  = handle->memory_type;

    /* XMC shares Micron's maker ID, and Spansion's FL1-K is made by Winbond. */
    return (maker == SPIF_MANUFACTURER_WINBOND) || (maker == SPIF_MANUFACTURER_GIGADEVICE) ||
           (maker == SPIF_MANUFACTURER_ISSI) || (maker == SPIF_MANUFACTURER_PUYA) ||
           (maker == SPIF_MANUFACTURER_ZBIT) || (maker == SPIF_MANUFACTURER_BOYA) ||
           ((maker == SPIF_MANUFACTURER_XMC) && ((type == 0x40U) || (type == 0x70U))) ||
           ((maker == SPIF_MANUFACTURER_ATMEL) && ((type & 0xE0U) == 0x80U)) ||
           ((maker == SPIF_MANUFACTURER_SPANSION) && (type == 0x40U));
}

/*****************************************************************************************************/
/**
 * @brief Check that a transfer stays inside the chip.
 *
 * @param[in] handle   Handle of the chip.
 * @param[in] address  First byte of the transfer.
 * @param[in] len      Length of the transfer.
 * @return SPIF_ERR_NONE or SPIF_ERR_RANGE.
 */
static spif_err_t spif_range(const spif_t *handle, uint32_t address, size_t len)
{
    spif_err_t err = SPIF_ERR_NONE;

    /* Written as a subtraction, because address + len can wrap past zero and
       then look small enough. Sent anyway, the chip would wrap round to its
       own start. */
    if ((address > handle->size) || (len > (size_t)(handle->size - address)))
    {
        err = SPIF_ERR_RANGE;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief How much of what is left fits before the next boundary.
 *
 * @param[in] address   Where this piece starts.
 * @param[in] left      Bytes still to go.
 * @param[in] boundary  Page size.
 * @return Bytes for this piece.
 */
static uint32_t spif_chunk(uint32_t address, size_t left, uint32_t boundary)
{
    /* From here to the next boundary, so the first piece of a write that
       starts inside a page only fills the rest of that page. */
    uint32_t room  = boundary - (address % boundary);
    uint32_t chunk = room;

    /* Or less, when that is all there is left. */
    if (left < (size_t)room)
    {
        chunk = (uint32_t)left;
    }

    return chunk;
}

/*****************************************************************************************************/
/**
 * @brief Send a command that is one byte and nothing else, with its own chip select.
 *
 * @param[in,out] handle   Handle of the chip.
 * @param[in]     command  The command.
 * @return SPIF_ERR_NONE, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_simple(spif_t *handle, uint8_t command)
{
    spif_err_t err = SPIF_ERR_NONE;

    spif_select(handle, true);
    err = spif_command(handle, command, 0U, 0U, 0U);
    spif_select(handle, false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Send a command with its address and dummy bytes. The chip must be selected.
 *
 * @param[in,out] handle         Handle of the chip.
 * @param[in]     command        The command.
 * @param[in]     address        The address, when there is one.
 * @param[in]     address_bytes  How many address bytes: 0 to 4.
 * @param[in]     dummy_bytes    Zero bytes after the address, so the total stays at 5.
 * @return SPIF_ERR_NONE, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_command(spif_t *handle, uint8_t command, uint32_t address,
                               uint8_t address_bytes, uint8_t dummy_bytes)
{
    uint8_t tx[SPIF_COMMAND_MAX] = { 0U, 0U, 0U, 0U, 0U, 0U };
    size_t  len                  = 1U;
    uint8_t i                    = 0U;

    tx[0] = command;

    /* The address goes most significant byte first. */
    for (i = address_bytes; i > 0U; i--)
    {
        tx[len] = (uint8_t)(address >> (8U * (i - 1U)));
        len++;
    }

    /* The chip ignores what a dummy byte holds, so the zeros already there do. */
    len += dummy_bytes;

    return spif_transfer(handle, tx, len, false);
}

/*****************************************************************************************************/
/**
 * @brief Read a one byte register, with its own chip select.
 *
 * @param[in,out] handle   Handle of the chip.
 * @param[in]     command  The command that reads it.
 * @param[out]    value    The register.
 * @return SPIF_ERR_NONE, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_register(spif_t *handle, uint8_t command, uint8_t *value)
{
    spif_err_t err = SPIF_ERR_NONE;

    spif_select(handle, true);
    err = spif_command(handle, command, 0U, 0U, 0U);

    if (err == SPIF_ERR_NONE)
    {
        err = spif_transfer(handle, value, 1U, true);
    }

    spif_select(handle, false);

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Send or receive len bytes by polling, in pieces the HAL can count.
 *
 * @param[in,out] handle   Handle of the chip.
 * @param[in,out] data     What to send, or where what is received goes.
 * @param[in]     len      How many bytes.
 * @param[in]     receive  Receive rather than send.
 * @return SPIF_ERR_NONE, or the SPIF_ERR_ value that says what went wrong.
 */
static spif_err_t spif_transfer(spif_t *handle, uint8_t *data, size_t len, bool receive)
{
    spif_err_t err  = SPIF_ERR_NONE;
    size_t     done = 0U;

    while ((err == SPIF_ERR_NONE) && (done < len))
    {
        size_t            chunk  = len - done;
        HAL_StatusTypeDef status = HAL_ERROR;

        if (chunk > SPIF_TRANSFER_LIMIT)
        {
            chunk = SPIF_TRANSFER_LIMIT;
        }

        /* Each piece gets the time spif_config.h allows a transfer. */
        status = receive ? HAL_SPI_Receive(handle->hspi, &data[done], (uint16_t)chunk,
                                           SPIF_TIMEOUT_TRANSFER_MS)
                         : HAL_SPI_Transmit(handle->hspi, &data[done], (uint16_t)chunk,
                                            SPIF_TIMEOUT_TRANSFER_MS);
        err    = spif_hal_error(status);
        done  += chunk;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Wait until the chip has finished writing a register, at init.
 *
 * @param[in,out] handle      Handle of the chip.
 * @param[in]     timeout_ms  Time allowed.
 * @return SPIF_ERR_NONE, SPIF_ERR_TIMEOUT, SPIF_ERR_PROTECTED or SPIF_ERR_SPI.
 */
static spif_err_t spif_wait_ready(spif_t *handle, uint32_t timeout_ms)
{
    uint32_t   start = HAL_GetTick();
    spif_err_t err   = SPIF_ERR_NONE;
    bool       busy  = true;

    while ((err == SPIF_ERR_NONE) && busy)
    {
        /* Sleep first: a page takes about half a millisecond and an erase far
           longer, so asking straight away would only find it busy. With an
           RTOS other threads run meanwhile, and without one it is HAL_Delay(). */
        osal_delay_ms(SPIF_POLL_MS);

        err = spif_busy(handle, &busy);

        /* Asked first, so a chip that finishes right at the end still counts. */
        if ((err == SPIF_ERR_NONE) && busy && (spif_remaining(start, timeout_ms) == 0U))
        {
            err = SPIF_ERR_TIMEOUT;
        }
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Ask the chip once whether it is still busy, and whether it refused what it was told.
 *
 * @param[in,out] handle  Handle of the chip.
 * @param[out]    busy    true while it programs or erases.
 * @return SPIF_ERR_NONE, SPIF_ERR_PROTECTED when the chip refused, or SPIF_ERR_SPI.
 */
static spif_err_t spif_busy(spif_t *handle, bool *busy)
{
    spif_err_t err   = SPIF_ERR_NONE;
    uint8_t    value = 0U;
    uint32_t   dice  = spif_dice(handle);
    uint32_t   die   = 0U;

    *busy = false;

    if (handle->family == SPIF_FAMILY_MICRON)
    {
        /* Micron's flag status register covers every die, and says when the
           chip refused or failed. */
        err = spif_register(handle, SPIF_CMD_READ_FLAGS, &value);

        if (err == SPIF_ERR_NONE)
        {
            *busy = ((value & SPIF_FLAG_READY) == 0U);
        }

        if ((err == SPIF_ERR_NONE) && !*busy && ((value & SPIF_FLAG_ERRORS) != 0U))
        {
            err = spif_refused(handle, SPIF_CMD_CLEAR_FLAGS);
        }
    }
    else
    {
        /* Status register 1. On a stacked Winbond chip each die answers only
           for itself, so each is selected and asked in turn. */
        for (die = 0U; (err == SPIF_ERR_NONE) && (die < dice); die++)
        {
            if (dice > 1U)
            {
                spif_select(handle, true);
                err = spif_command(handle, SPIF_CMD_SELECT_DIE, die, 1U, 0U);
                spif_select(handle, false);
            }

            if (err == SPIF_ERR_NONE)
            {
                err = spif_register(handle, SPIF_CMD_READ_STATUS, &value);
            }

            if ((err == SPIF_ERR_NONE) && (handle->family == SPIF_FAMILY_SPANSION) &&
                ((value & SPIF_STATUS_ERRORS) != 0U))
            {
                /* Refused, and busy until told the error has been seen. */
                err = spif_refused(handle, SPIF_CMD_CLEAR_STATUS);
            }
            else if ((err == SPIF_ERR_NONE) && ((value & SPIF_STATUS_BUSY) != 0U))
            {
                *busy = true;
            }
            else
            {
                /* This die is ready, or its status could not be read. */
            }
        }
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Clear the error of a refused program or erase, and the write enable left set after it.
 *
 * @param[in,out] handle  Handle of the chip.
 * @param[in]     clear   The command that clears the error.
 * @return SPIF_ERR_PROTECTED, or SPIF_ERR_SPI when the clearing failed.
 */
static spif_err_t spif_refused(spif_t *handle, uint8_t clear)
{
    spif_err_t err = spif_simple(handle, clear);

    if (err == SPIF_ERR_NONE)
    {
        err = spif_simple(handle, SPIF_CMD_WRITE_DISABLE);
    }

    if (err == SPIF_ERR_NONE)
    {
        err = SPIF_ERR_PROTECTED;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief How many dies have to be asked whether they are busy.
 *
 * @param[in] handle  Handle of the chip.
 * @return Dies in a stacked Winbond chip, otherwise 1.
 */
static uint32_t spif_dice(const spif_t *handle)
{
    uint32_t dice = 1U;

    if ((handle->manufacturer == SPIF_MANUFACTURER_WINBOND) && (handle->size > SPIF_DIE_SIZE))
    {
        dice = handle->size / SPIF_DIE_SIZE;
    }

    return dice;
}

/*****************************************************************************************************/
/**
 * @brief Say what a HAL status means in spif's terms.
 *
 * @param[in] status  What the HAL returned.
 * @return SPIF_ERR_NONE, SPIF_ERR_TIMEOUT or SPIF_ERR_SPI.
 */
static spif_err_t spif_hal_error(HAL_StatusTypeDef status)
{
    spif_err_t err = SPIF_ERR_SPI;

    if (status == HAL_OK)
    {
        err = SPIF_ERR_NONE;
    }
    else if (status == HAL_TIMEOUT)
    {
        err = SPIF_ERR_TIMEOUT;
    }
    else
    {
        /* HAL_ERROR, or HAL_BUSY from an SPI still busy with something else. */
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Milliseconds left of a timeout.
 *
 * @param[in] start       HAL_GetTick() when the wait began.
 * @param[in] timeout_ms  Time allowed.
 * @return Milliseconds left, or 0 once it has run out.
 */
static uint32_t spif_remaining(uint32_t start, uint32_t timeout_ms)
{
    /* A subtraction, which stays right when the tick wraps after 49 days. */
    uint32_t elapsed = HAL_GetTick() - start;
    uint32_t left    = 0U;

    if (elapsed < timeout_ms)
    {
        left = timeout_ms - elapsed;
    }

    return left;
}

/*****************************************************************************************************/
/**
 * @brief Select the chip, or let it go.
 *
 * @param[in] handle  Handle of the chip.
 * @param[in] select  true selects it, by driving chip select low.
 */
static void spif_select(const spif_t *handle, bool select)
{
    HAL_GPIO_WritePin(handle->cs_port, handle->cs_pin, select ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
