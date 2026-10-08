/**
 * @file        test_spif.c
 * @brief       Host unit tests for the spif library, built on Unity.
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
 * @note        Runs on a PC, not on hardware. The HAL calls land on a model of
 *              an SPI NOR flash that follows the datasheets: it programs only
 *              after a write enable and only clears bits, a write wraps inside
 *              its page, a busy chip takes nothing but a status read, and a
 *              chip over 16 MB takes 4 byte addresses the way its maker does.
 *              The model can also be a Micron, a Spansion FL-S, an old M25P,
 *              an SST26, an AT25DF or a stacked Winbond, each with its own
 *              registers, erase sizes, locks and unique ID. Time is a clock the
 *              tests move, so every wait is checked instantly, DMA included.
 *              osal is a fake the tests control.
 *
 *              Built twice, once for each SPIF_TRANSFER setting. Every read,
 *              write and erase is finished with spif_wait(), as a user would,
 *              so most tests run unchanged in both. A test that only means
 *              something with DMA is left out of the polling build. A third
 *              build sets the sector and chip erase times to HAL_MAX_DELAY,
 *              and runs only the tests that it waits for ever.
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
#include "spif.h"

/*
 * ****************************************************************************************************
 * Macros
 * ****************************************************************************************************
*/

/* The largest chip whose memory the model keeps, 32 MB, a 256 Mbit part. A
   bigger chip still works: what lies beyond is not stored, and reads as 0xFF. */
#define CHIP_MAX_BYTES      0x2000000UL

/* One die of a stacked chip. */
#define DIE_BYTES           0x4000000UL
#define DICE_MAX            4

/* One byte on the bus at 8 MHz. */
#define BYTE_US             1U

/* How long the modelled chip takes, near a W25Q's typical times. */
#define PAGE_US             700U
#define SECTOR_US           45000U
#define BLOCK32_US          120000U
#define BLOCK_US            150000U
#define CHIP_US             20000000U
#define STATUS_US           2000U

/* What the model takes in one transaction: a command, its address and a page. */
#define BUF_MAX             300U

/* How many commands and programs the model remembers the details of. */
#define RECORD_MAX          512U

/* Big enough for the longest read the tests make. */
#define PATTERN_BYTES       131072U

#define CS_PIN              GPIO_PIN_4

/* A 128 Mbit chip, which takes 3 byte addresses, and a 256 Mbit one, which
   takes 4. */
#define CHIP_16MB           0x18U
#define CHIP_32MB           0x19U

/* Erase sizes. */
#define KB4                 0x1000UL
#define KB32                0x8000UL
#define KB64                0x10000UL
#define KB256               0x40000UL

/* Whether this build moves read and write data by DMA. */
#define USES_DMA            (SPIF_TRANSFER == SPIF_TRANSFER_DMA)

/* Whether this build waits for ever for an erase, as HAL_MAX_DELAY says. */
#define WAITS_FOR_EVER      (SPIF_TIMEOUT_SECTOR_MS == HAL_MAX_DELAY)

/* Public calls that take a pointer, each tried with a NULL. */
#define NULL_CALLS          15

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief The kinds of chip the model can be.
 */
typedef enum
{
    KIND_WINBOND = 0, /**< Winbond, and every chip like it.                 */
    KIND_NO_ID,       /**< Like Winbond, with no unique ID: Macronix.       */
    KIND_MICRON,      /**< A flag status register, errors to clear.         */
    KIND_M25P,        /**< Big sectors only, erased with D8h.               */
    KIND_SPANSION,    /**< FL-S: big sectors, errors that keep it busy.     */
    KIND_SST26,       /**< Locked at power up, its own ID and erase layout. */
    KIND_AT25DF,      /**< Locked at power up, unlocked through status.     */

} chip_kind_t;

/*****************************************************************************************************/
/**
 * @brief An SPI NOR flash on the bus, and what happened to it.
 */
typedef struct
{
    chip_kind_t       kind;               /**< Which maker's ways it follows.               */
    uint32_t          size;               /**< Bytes.                                       */
    uint8_t           id[6];              /**< What 9Fh sends.                              */
    uint32_t          big_sector;         /**< What D8h erases on an M25P or a Spansion.    */
    int               dice;               /**< Dies, more than 1 on a stacked chip.         */
    int               die;                /**< The die C2h selected.                        */
    bool              present;            /**< Whether it is on the bus at all.             */
    bool              has_4b_commands;    /**< Takes 0Ch, 12h, 21h and DCh.                 */
    bool              has_4byte_mode;     /**< Takes B7h.                                   */
    bool              mode_needs_wren;    /**< B7h only after a write enable, as Micron.    */
    bool              mode_4byte;         /**< Switched to 4 byte addresses.                */
    bool              wel;                /**< Write enable latch.                          */
    bool              never_ready;        /**< Stays busy for ever once it starts.          */
    uint8_t           bp;                 /**< BP0 to BP2: non zero protects the chip.      */
    bool              locked;             /**< SST26 or AT25DF lock, set at power up.       */
    bool              locked_for_ever;    /**< SST26: a block locked for good.              */
    bool              sprl;               /**< AT25DF: the lock on the protection.          */
    bool              wp_low;             /**< The WP pin held low.                         */
    uint8_t           flags;              /**< Micron flag status error bits.               */
    uint8_t           errors;             /**< Spansion status error bits.                  */
    uint32_t          busy_until_us[DICE_MAX]; /**< When each die ends what it does.        */
    uint32_t          page_us;            /**< How long a page takes.                       */
    uint32_t          sector_us;          /**< A 4 KB erase.                                */
    uint32_t          block32_us;         /**< A 32 KB erase.                               */
    uint32_t          block_us;           /**< A D8h erase.                                 */
    uint32_t          chip_us;            /**< A chip erase.                                */
    uint8_t           unique_id[SPIF_UNIQUE_ID_SIZE]; /**< The ID from the factory.         */
    uint8_t           otp[128];           /**< Spansion and AT25DF OTP areas.               */

    bool              cs_low;             /**< Selected.                                    */
    bool              ignored;            /**< This transaction came while it was busy.     */
    uint8_t           buf[BUF_MAX];       /**< What it took in this transaction.            */
    uint32_t          buf_len;            /**< How much of buf is used.                     */
    uint32_t          clocked;            /**< Bytes clocked this transaction, both ways.   */

    uint32_t          last_timeout;       /**< Timeout the last polling HAL call got.       */
    int               fail_at;            /**< SPI call that fails, 0 for none.             */
    HAL_StatusTypeDef fail_with;          /**< What that call returns.                      */
    bool              dma_running;        /**< A DMA transfer has not finished yet.         */
    bool              dma_stall;          /**< DMA transfers never finish.                  */
    uint32_t          dma_done_us;        /**< When the running one finishes.               */
    uint32_t          dma_error;          /**< What HAL_SPI_GetError() reports.             */

    int               transfers;          /**< SPI calls of every kind.                     */
    int               dma_transfers;      /**< SPI calls by DMA.                            */
    int               dma_aborts;         /**< HAL_SPI_Abort() calls.                       */
    int               selects;            /**< Times chip select went low.                  */
    int               programs;           /**< Pages programmed.                            */
    int               erases;             /**< Erases done.                                 */
    int               refusals;           /**< Programs and erases refused.                 */
    int               count[256];         /**< Each command, how often it came.             */
    uint32_t          largest_transfer;   /**< The longest single HAL transfer.             */
    uint8_t           log[RECORD_MAX];    /**< The first commands, in order.                */
    int               log_len;            /**< How many are in log.                         */
    uint32_t          program_sizes[RECORD_MAX]; /**< Bytes in each page programmed.        */
    int               faults;             /**< Things a real chip would get wrong.          */
    const char        *fault;             /**< What the first of them was.                  */

} fake_chip_t;

/*
 * ****************************************************************************************************
 * Global variables
 * ****************************************************************************************************
*/

static fake_chip_t       chip;
static uint8_t           chip_mem[CHIP_MAX_BYTES];
static spif_t            flash;
static SPI_HandleTypeDef test_spi;
static GPIO_TypeDef      test_cs_port;

static uint32_t          now_us = 0U;

/* Added to the tick, so a test can move it on by days at once. now_us alone
   only reaches 71 minutes. */
static uint32_t          tick_jump_ms = 0U;

static uint8_t           pattern[PATTERN_BYTES];
static uint8_t           readback[PATTERN_BYTES];

static const uint8_t     factory_id[SPIF_UNIQUE_ID_SIZE] = { 0xD1U, 0x62U, 0x3BU, 0x04U,
                                                             0xC5U, 0x76U, 0x07U, 0xE8U };

/* The fake osal. */
static osal_err_t         lock_result        = OSAL_ERR_NONE;
static bool               mutex_create_fails = false;
static const osal_mutex_t *mutex_created_at  = NULL;
static int                mutex_creates      = 0;
static int                mutex_takes        = 0;
static int                mutex_gives        = 0;
static int                mutex_held         = 0;
static uint32_t           mutex_last_wait    = 0U;
static int                sleeps             = 0;

/* assert_param, as CubeMX builds it with Enable Full Assert. */
static jmp_buf assert_return;
static bool    assert_expected = false;
static int     asserts         = 0;

/*
 * ****************************************************************************************************
 * Private function prototypes
 * ****************************************************************************************************
*/

static uint32_t   chip_size_of(uint8_t capacity);
static void       chip_new(chip_kind_t kind, uint8_t maker, uint8_t type, uint8_t capacity,
                           uint32_t size);
static void       chip_reset(uint8_t manufacturer, uint8_t capacity);
static void       counts_clear(void);
static void       chip_fault(const char *what);
static bool       die_busy(int die);
static bool       chip_busy(void);
static int        die_of(uint32_t address);
static bool       chip_is_4b(uint8_t opcode);
static uint32_t   chip_address_bytes(uint8_t opcode);
static uint32_t   chip_address(void);
static void       chip_take(uint8_t byte);
static uint8_t    chip_give(void);
static uint8_t    chip_status(void);
static uint8_t    chip_id_byte(uint32_t position);
static void       chip_execute(void);
static void       chip_write_status(void);
static bool       chip_refuses(bool program);
static void       chip_program(void);
static void       chip_erase(uint32_t unit, uint32_t busy_us);
static bool       chip_spi_begin(const SPI_HandleTypeDef *hspi, uint16_t size,
                                 HAL_StatusTypeDef *fail);
static spif_err_t init_chip(uint8_t manufacturer, uint8_t capacity);
static spif_err_t init_kind(chip_kind_t kind, uint8_t maker, uint8_t type, uint8_t capacity,
                            uint32_t size);
static spif_err_t settle(spif_err_t err);
static spif_err_t read_all(uint32_t address, uint8_t *data, size_t len);
static spif_err_t write_all(uint32_t address, const uint8_t *data, size_t len);
static spif_err_t erase_sector(uint32_t sector);
static spif_err_t erase_block32(uint32_t block32);
static spif_err_t erase_block(uint32_t block);
static spif_err_t erase_chip(void);
static void       pattern_fill(uint32_t seed);
static bool       assert_stops(int which);

/*
 * ****************************************************************************************************
 * Public function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Where assert_param lands with USE_FULL_ASSERT, as the user's main.c defines it.
 *
 * @param[in] file  Source file of the assert.
 * @param[in] line  Its line.
 */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;

    asserts++;

    if (!assert_expected)
    {
        TEST_FAIL_MESSAGE("assert_param stopped a valid call");
    }

    longjmp(assert_return, 1);
}

/*****************************************************************************************************/
/**
 * @brief Return the tick, from the tests' clock.
 *
 * @return Milliseconds since the clock was reset.
 */
uint32_t HAL_GetTick(void)
{
    return (now_us / 1000U) + tick_jump_ms;
}

/*****************************************************************************************************/
/**
 * @brief Wait, by moving the clock on. spif has to sleep through osal instead.
 *
 * @param[in] Delay  Milliseconds.
 */
void HAL_Delay(uint32_t Delay)
{
    /* With an RTOS this spins, and keeps every other thread waiting. */
    chip_fault("waited in HAL_Delay() and not through osal");

    now_us += Delay * 1000U;
}

/*****************************************************************************************************/
/**
 * @brief Drive chip select. Selecting starts a transaction, letting go ends it.
 *
 * @param[in] GPIOx     Port.
 * @param[in] GPIO_Pin  Pin.
 * @param[in] PinState  GPIO_PIN_RESET selects the chip.
 */
void HAL_GPIO_WritePin(GPIO_TypeDef *GPIOx, uint16_t GPIO_Pin, GPIO_PinState PinState)
{
    if ((GPIOx != &test_cs_port) || (GPIO_Pin != CS_PIN))
    {
        chip_fault("a pin other than chip select was driven");
    }
    else if (PinState == GPIO_PIN_RESET)
    {
        if (chip.cs_low)
        {
            chip_fault("selected a chip already selected");
        }

        chip.cs_low  = true;
        chip.ignored = false;
        chip.buf_len = 0U;
        chip.clocked = 0U;
        chip.selects++;
    }
    else
    {
        if (chip.dma_running)
        {
            chip_fault("let the chip go while its DMA was still running");
        }

        /* A chip acts on what it was told when chip select goes high. */
        if (chip.cs_low)
        {
            chip_execute();
        }

        chip.cs_low = false;
    }
}

/*****************************************************************************************************/
/**
 * @brief Send to the chip, spending the bus time on the clock.
 *
 * @return HAL_OK, or what the test chose for a failing call.
 */
HAL_StatusTypeDef HAL_SPI_Transmit(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size,
                                   uint32_t Timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t          i      = 0U;

    chip.last_timeout = Timeout;

    if (chip_spi_begin(hspi, Size, &status))
    {
        for (i = 0U; i < Size; i++)
        {
            chip_take(pData[i]);
        }

        now_us += (uint32_t)Size * BYTE_US;
    }

    return status;
}

/*****************************************************************************************************/
/**
 * @brief Receive from the chip, spending the bus time on the clock.
 *
 * @return HAL_OK, or what the test chose for a failing call.
 */
HAL_StatusTypeDef HAL_SPI_Receive(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size,
                                  uint32_t Timeout)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t          i      = 0U;

    chip.last_timeout = Timeout;

    if (chip_spi_begin(hspi, Size, &status))
    {
        for (i = 0U; i < Size; i++)
        {
            pData[i] = chip_give();
        }

        now_us += (uint32_t)Size * BYTE_US;
    }

    return status;
}

/*****************************************************************************************************/
/**
 * @brief Send to the chip by DMA. The bytes move at once, the DMA finishes later on the clock.
 *
 * @return HAL_OK, or what the test chose for a failing call.
 */
HAL_StatusTypeDef HAL_SPI_Transmit_DMA(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t          i      = 0U;

    if (chip_spi_begin(hspi, Size, &status))
    {
        for (i = 0U; i < Size; i++)
        {
            chip_take(pData[i]);
        }

        chip.dma_transfers++;
        chip.dma_running = true;
        chip.dma_done_us = now_us + ((uint32_t)Size * BYTE_US);
    }

    return status;
}

/*****************************************************************************************************/
/**
 * @brief Receive from the chip by DMA. The bytes move at once, the DMA finishes later on the clock.
 *
 * @return HAL_OK, or what the test chose for a failing call.
 */
HAL_StatusTypeDef HAL_SPI_Receive_DMA(SPI_HandleTypeDef *hspi, uint8_t *pData, uint16_t Size)
{
    HAL_StatusTypeDef status = HAL_OK;
    uint16_t          i      = 0U;

    if (chip_spi_begin(hspi, Size, &status))
    {
        for (i = 0U; i < Size; i++)
        {
            pData[i] = chip_give();
        }

        chip.dma_transfers++;
        chip.dma_running = true;
        chip.dma_done_us = now_us + ((uint32_t)Size * BYTE_US);
    }

    return status;
}

/*****************************************************************************************************/
/**
 * @brief Busy while a DMA transfer runs, ready once the clock has passed its end.
 *
 * @return HAL_SPI_STATE_BUSY_TX_RX or HAL_SPI_STATE_READY.
 */
HAL_SPI_StateTypeDef HAL_SPI_GetState(SPI_HandleTypeDef *hspi)
{
    HAL_SPI_StateTypeDef state = HAL_SPI_STATE_READY;

    if (hspi != &test_spi)
    {
        chip_fault("asked the state of the wrong SPI handle");
    }

    if (chip.dma_running && (chip.dma_stall || (now_us < chip.dma_done_us)))
    {
        state = HAL_SPI_STATE_BUSY_TX_RX;
    }
    else
    {
        chip.dma_running = false;
    }

    return state;
}

/*****************************************************************************************************/
/**
 * @brief The error the last DMA transfer ended with, as the test chose.
 *
 * @return HAL_SPI_ERROR_NONE, or the test's choice.
 */
uint32_t HAL_SPI_GetError(SPI_HandleTypeDef *hspi)
{
    (void)hspi;

    return chip.dma_error;
}

/*****************************************************************************************************/
/**
 * @brief Stop a DMA transfer.
 *
 * @return HAL_OK.
 */
HAL_StatusTypeDef HAL_SPI_Abort(SPI_HandleTypeDef *hspi)
{
    (void)hspi;

    chip.dma_running = false;
    chip.dma_aborts++;

    return HAL_OK;
}

/*****************************************************************************************************/
/**
 * @brief Create the fake mutex, unless the test says it cannot be made.
 *
 * @param[out] mutex  Mutex to create.
 * @return OSAL_ERR_NONE, or OSAL_ERR_MUTEX when the test says so.
 */
osal_err_t osal_mutex_create(osal_mutex_t *mutex)
{
    osal_err_t err = OSAL_ERR_MUTEX;

    mutex_creates++;

    if (!mutex_create_fails)
    {
        mutex_created_at = mutex;
        err              = OSAL_ERR_NONE;
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Take the fake mutex, keeping count, and give the answer the test chose.
 *
 * @param[in,out] mutex       Mutex to take.
 * @param[in]     timeout_ms  The wait it was given.
 * @return The answer the test chose.
 */
osal_err_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms)
{
    if ((mutex == NULL) || (mutex != mutex_created_at))
    {
        chip_fault("asked for a mutex init never made");
    }

    if (mutex_held != 0)
    {
        chip_fault("took a mutex it already held");
    }

    mutex_takes++;
    mutex_last_wait = timeout_ms;

    if (lock_result == OSAL_ERR_NONE)
    {
        mutex_held++;
    }

    return lock_result;
}

/*****************************************************************************************************/
/**
 * @brief Give the fake mutex back, keeping count.
 *
 * @param[in,out] mutex  Mutex to give back.
 */
void osal_mutex_unlock(osal_mutex_t *mutex)
{
    if ((mutex == NULL) || (mutex != mutex_created_at))
    {
        chip_fault("gave back a mutex init never made");
    }

    if (mutex_held == 0)
    {
        chip_fault("gave back a mutex it did not hold");
    }
    else
    {
        mutex_held--;
    }

    mutex_gives++;
}

/*****************************************************************************************************/
/**
 * @brief Sleep, by moving the clock on.
 *
 * @param[in] ms  Milliseconds.
 */
void osal_delay_ms(uint32_t ms)
{
    sleeps++;
    now_us += ms * 1000U;
}

/*****************************************************************************************************/
/**
 * @brief A new chip and a fresh handle before every test.
 */
void setUp(void)
{
    now_us             = 0U;
    tick_jump_ms       = 0U;
    lock_result        = OSAL_ERR_NONE;
    mutex_create_fails = false;
    mutex_created_at   = NULL;
    mutex_creates      = 0;
    assert_expected    = false;
    asserts            = 0;

    memset(&flash, 0, sizeof(flash));
    pattern_fill(0U);
    chip_reset(SPIF_MANUFACTURER_WINBOND, CHIP_16MB);
    counts_clear();
}

/*****************************************************************************************************/
/**
 * @brief What must hold after every test, whatever it did.
 */
void tearDown(void)
{
    TEST_ASSERT_FALSE_MESSAGE(chip.cs_low, "left the chip selected");
    TEST_ASSERT_FALSE_MESSAGE(chip.dma_running, "left a DMA transfer running");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mutex_held, "kept the mutex");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, chip.faults, chip.fault);
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Init
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief Every capacity code a chip can send becomes the right size, from 1 Mbit to 2 Gbit.
 */
void test_init_reads_the_size_from_the_chip(void)
{
    static const uint8_t codes[] = { 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U, 0x18U,
                                     0x19U, 0x1AU, 0x1BU, 0x1CU, 0x20U, 0x21U, 0x22U };
    size_t               i       = 0U;

    for (i = 0U; i < (sizeof(codes) / sizeof(codes[0])); i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, codes[i]),
                                      "a real size was refused");
        TEST_ASSERT_EQUAL_HEX32(chip_size_of(codes[i]), flash.size);
        TEST_ASSERT_EQUAL_HEX32(KB4, flash.sector_size);
        TEST_ASSERT_EQUAL_HEX8(SPIF_MANUFACTURER_WINBOND, flash.manufacturer);
        TEST_ASSERT_EQUAL_HEX8(0x40U, flash.memory_type);
    }
}

/*****************************************************************************************************/
/**
 * @brief The makers that give their size another way are read right too.
 */
void test_other_size_codes_are_read(void)
{
    static const struct
    {
        chip_kind_t kind;
        uint8_t     maker;
        uint8_t     type;
        uint8_t     capacity;
        uint32_t    size;
    } chips[] = {
        { KIND_NO_ID,    SPIF_MANUFACTURER_MACRONIX, 0x25U, 0x32U, 0x40000UL    }, /* MX25U2033E  */
        { KIND_NO_ID,    SPIF_MANUFACTURER_MACRONIX, 0x25U, 0x38U, 0x1000000UL  }, /* MX25U12835F */
        { KIND_NO_ID,    SPIF_MANUFACTURER_MACRONIX, 0x25U, 0x3CU, 0x10000000UL }, /* MX66U2G45G  */
        { KIND_AT25DF,   SPIF_MANUFACTURER_ATMEL,    0x44U, 0x01U, 0x80000UL    }, /* AT25DF041A  */
        { KIND_AT25DF,   SPIF_MANUFACTURER_ATMEL,    0x47U, 0x01U, 0x400000UL   }, /* AT25DF321A  */
        { KIND_AT25DF,   SPIF_MANUFACTURER_ATMEL,    0x48U, 0x00U, 0x800000UL   }, /* AT25DF641   */
        { KIND_WINBOND,  SPIF_MANUFACTURER_ATMEL,    0x87U, 0x01U, 0x400000UL   }, /* AT25SF321   */
        { KIND_WINBOND,  SPIF_MANUFACTURER_ATMEL,    0x42U, 0x16U, 0x400000UL   }, /* AT25SL321   */
        { KIND_SST26,    SPIF_MANUFACTURER_SST,      0x26U, 0x41U, 0x200000UL   }, /* SST26VF016B */
        { KIND_SST26,    SPIF_MANUFACTURER_SST,      0x26U, 0x42U, 0x400000UL   }, /* SST26VF032B */
        { KIND_SST26,    SPIF_MANUFACTURER_SST,      0x26U, 0x43U, 0x800000UL   }, /* SST26VF064B */
        { KIND_SST26,    SPIF_MANUFACTURER_SST,      0x26U, 0x51U, 0x200000UL   }, /* SST26WF016B */
        { KIND_MICRON,   SPIF_MANUFACTURER_MICRON,   0xBAU, 0x19U, 0x2000000UL  }, /* MT25QL256   */
        { KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, 0x20U, 0x18U, 0x1000000UL  }, /* S25FL128S   */
    };
    size_t i = 0U;

    for (i = 0U; i < (sizeof(chips) / sizeof(chips[0])); i++)
    {
        TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE,
                                      init_kind(chips[i].kind, chips[i].maker, chips[i].type,
                                                chips[i].capacity, chips[i].size),
                                      "a real chip was refused");
        TEST_ASSERT_EQUAL_HEX32(chips[i].size, flash.size);
    }
}

/*****************************************************************************************************/
/**
 * @brief A size not known, a chip this driver cannot drive, or no chip at all, is refused.
 */
void test_a_chip_not_known_is_refused(void)
{
    static const uint8_t ids[][3] = {
        { SPIF_MANUFACTURER_WINBOND, 0x40U, 0x00U },
        { SPIF_MANUFACTURER_WINBOND, 0x40U, 0x10U },
        { SPIF_MANUFACTURER_WINBOND, 0x40U, 0x1DU },
        { SPIF_MANUFACTURER_WINBOND, 0x40U, 0x23U },
        { SPIF_MANUFACTURER_WINBOND, 0x40U, 0xFFU },
        { SPIF_MANUFACTURER_SST,     0x25U, 0x41U }, /* SST25VF016B, a byte at a time */
        { SPIF_MANUFACTURER_SST,     0x26U, 0x44U }, /* an SST26 not known            */
        { SPIF_MANUFACTURER_ATMEL,   0x25U, 0x00U }, /* AT45DB081D, DataFlash         */
        { 0x34U,                     0x2AU, 0x1AU }, /* Infineon Semper S25HL512T     */
        { SPIF_MANUFACTURER_EON,     0x30U, 0x41U }, /* a size code not known         */
    };
    size_t i = 0U;

    for (i = 0U; i < (sizeof(ids) / sizeof(ids[0])); i++)
    {
        TEST_ASSERT_EQUAL_INT(SPIF_ERR_CHIP,
                              init_kind(KIND_WINBOND, ids[i][0], ids[i][1], ids[i][2], 0x100000UL));
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, mutex_creates, "a mutex made for a chip not found");
        TEST_ASSERT_EQUAL_HEX8_MESSAGE(ids[i][0], flash.manufacturer,
                                       "what the chip sent was not kept");
        TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, read_all(0U, readback, 1U));
    }
}

/*****************************************************************************************************/
/**
 * @brief No chip on the bus reads as all ones, and is reported.
 */
void test_no_chip_is_reported(void)
{
    chip.present = false;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_CHIP, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_EQUAL_HEX8(0xFFU, flash.manufacturer);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, write_all(0U, pattern, 1U));
}

/*****************************************************************************************************/
/**
 * @brief An SPI failure at any step of init is reported as one, and the handle is refused.
 *
 * On a 32 MB chip init makes seven transfers: the wake up, the JEDEC command and
 * its answer, the write enable and the switch to 4 byte addresses, and the
 * status read and its answer.
 */
void test_an_spi_failure_in_init_is_reported(void)
{
    int step = 0;

    for (step = 1; step <= 7; step++)
    {
        chip_reset(SPIF_MANUFACTURER_WINBOND, CHIP_32MB);
        chip.fail_at     = step;
        chip.fail_with   = HAL_ERROR;
        mutex_created_at = NULL;
        mutex_creates    = 0;

        TEST_ASSERT_EQUAL_INT(SPIF_ERR_SPI, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
        TEST_ASSERT_EQUAL_INT_MESSAGE(step, chip.transfers, "went on after a failed step");
        TEST_ASSERT_EQUAL_INT(0, mutex_creates);
        TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, read_all(0U, readback, 1U));
    }
}

/*****************************************************************************************************/
/**
 * @brief Init lets go of a chip left selected, by a pin CubeMX starts low.
 *
 * A chip only starts a command when chip select falls, so one left low takes
 * the wake up as part of whatever came before.
 */
void test_init_lets_go_of_a_chip_left_selected(void)
{
    chip.cs_low = true;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_EQUAL_HEX32(0x1000000U, flash.size);
}

/*****************************************************************************************************/
/**
 * @brief Straight after reset init waits for the chip to power up, sleeping through osal.
 */
void test_init_waits_for_power_up(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_TRUE_MESSAGE(chip.log_len > 0, "nothing was sent");
    TEST_ASSERT_TRUE_MESSAGE(HAL_GetTick() >= 20U, "spoke to the chip before it had powered up");

    /* Later on, there is nothing to wait for but the wake up. */
    chip_reset(SPIF_MANUFACTURER_WINBOND, CHIP_16MB);
    counts_clear();
    mutex_created_at = NULL;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, sleeps, "waited for power up again");
}

/*****************************************************************************************************/
/**
 * @brief Init wakes a chip from deep power down before it asks for the ID.
 */
void test_init_wakes_the_chip_first(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip_reset(SPIF_MANUFACTURER_WINBOND, CHIP_16MB);
    mutex_created_at = NULL;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));

    TEST_ASSERT_TRUE(chip.log_len >= 2);
    TEST_ASSERT_EQUAL_HEX8(0xABU, chip.log[0]);
    TEST_ASSERT_EQUAL_HEX8(0x9FU, chip.log[1]);
}

/*****************************************************************************************************/
/**
 * @brief A chip of 16 MB or less keeps 3 byte addresses and the usual commands.
 */
void test_up_to_16mb_uses_3_byte_addresses(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    TEST_ASSERT_EQUAL_UINT8(3U, flash.address_bytes);
    TEST_ASSERT_FALSE_MESSAGE(chip.mode_4byte, "a 16 MB chip was switched to 4 byte addresses");

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0xFFFF00U, pattern, 256U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0xFFFF00U, readback, 256U));
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0xFFFF00U], 256U);
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 256U);
}

/*****************************************************************************************************/
/**
 * @brief A chip over 16 MB is switched to 4 byte addresses, and needs none of the 4 byte commands.
 *
 * The W25Q256FV has no 12h, 21h or DCh, which the model leaves out here.
 */
void test_over_16mb_is_switched_to_4_byte_addresses(void)
{
    chip_reset(SPIF_MANUFACTURER_WINBOND, CHIP_32MB);
    chip.has_4b_commands = false;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_TRUE(chip.mode_4byte);
    TEST_ASSERT_EQUAL_UINT8(4U, flash.address_bytes);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(0x1800U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x1800010U, pattern, 300U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0x1800010U, readback, 300U));

    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(pattern, &chip_mem[0x1800010U], 300U,
                                     "landed somewhere else in the chip");
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 300U);
}

/*****************************************************************************************************/
/**
 * @brief Micron switches to 4 byte addresses only after a write enable, which it gets.
 */
void test_micron_gets_a_write_enable_before_the_switch(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_MICRON, SPIF_MANUFACTURER_MICRON, 0xBAU,
                                                   CHIP_32MB, 0x2000000UL));
    TEST_ASSERT_TRUE_MESSAGE(chip.mode_4byte, "Micron never left 3 byte addresses");
}

/*****************************************************************************************************/
/**
 * @brief Spansion has no switch, so it gets its own 4 byte commands, and a 32 KB erase of sectors.
 *
 * The S25FL256L has 4 KB sectors like a Winbond, but no 4 byte 32 KB erase.
 */
void test_spansion_uses_its_own_4_byte_commands(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_WINBOND, SPIF_MANUFACTURER_SPANSION, 0x60U,
                                                   CHIP_32MB, 0x2000000UL));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(0x180U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(0x1801U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x1800F00U, pattern, 0x200U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0x1800F00U, readback, 0x200U));
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x1800F00U], 0x200U);
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 0x200U);

    TEST_ASSERT_TRUE((chip.count[0x12] > 0) && (chip.count[0x0C] > 0) && (chip.count[0x21] > 0) &&
                     (chip.count[0xDC] > 0));

    counts_clear();
    memset(&chip_mem[0x1810000U], 0x00, KB32 + 1U);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block32(0x302U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(8, chip.count[0x21], "32 KB is eight 4 KB sectors");
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x1810000U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x1810000U + KB32 - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[0x1810000U + KB32]);
}

/*****************************************************************************************************/
/**
 * @brief Init makes one mutex, for the handle it sets up, and only for a chip it found.
 */
void test_init_makes_one_mutex(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    TEST_ASSERT_EQUAL_INT(1, mutex_creates);
    TEST_ASSERT_EQUAL_PTR(&flash.mutex, mutex_created_at);
}

/*****************************************************************************************************/
/**
 * @brief A mutex the RTOS cannot make is reported, and the handle is refused.
 */
void test_a_mutex_that_cannot_be_made_is_reported(void)
{
    mutex_create_fails = true;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_MUTEX, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, read_all(0U, readback, 1U));
}

/*****************************************************************************************************/
/**
 * @brief A handle that worked is refused after a later init of it fails.
 */
void test_a_failed_init_leaves_the_handle_refused(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    /* init runs before any mutex is held, as on a fresh handle. */
    chip.present     = false;
    mutex_created_at = NULL;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_CHIP, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, read_all(0U, readback, 1U));
}

/*****************************************************************************************************/
/**
 * @brief A handle full of rubbish works once spif_init() has run.
 */
void test_a_handle_full_of_rubbish_works_after_init(void)
{
    spif_t     local;
    spif_err_t err = SPIF_ERR_NONE;

    memset(&local, 0xA5, sizeof(local));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&local, &test_spi, &test_cs_port, CS_PIN));

    err = spif_write(&local, 10U, pattern, 8U);

    if (err == SPIF_ERR_NONE)
    {
        err = spif_wait(&local);
    }

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, err);
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[10U], 8U);
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Write protection
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief Write protection is reported by init, and the handle still reads.
 *
 * A Winbond gives no sign when it ignores a write, so init is the one place
 * spif can tell.
 */
void test_write_protection_is_reported(void)
{
    chip_reset(SPIF_MANUFACTURER_WINBOND, CHIP_16MB);
    chip.bp = 1U;
    memcpy(chip_mem, pattern, 64U);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_EQUAL_HEX32(0x1000000U, flash.size);
    TEST_ASSERT_EQUAL_INT(1, mutex_creates);
    counts_clear();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 64U));
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 64U);

    /* The chip ignores the write, and says nothing. */
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x1000U, pattern, 16U));
    TEST_ASSERT_EQUAL_INT(1, chip.refusals);
}

/*****************************************************************************************************/
/**
 * @brief An SST26, locked at every power up, is unlocked, unless a block is locked for ever.
 */
void test_sst26_is_unlocked_at_init(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_SST26, SPIF_MANUFACTURER_SST, 0x26U,
                                                   0x42U, 0x400000UL));
    TEST_ASSERT_FALSE_MESSAGE(chip.locked, "the SST26 was left locked");
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x100U, pattern, 64U));
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x100U], 64U);
    TEST_ASSERT_EQUAL_INT(0, chip.refusals);

    chip_new(KIND_SST26, SPIF_MANUFACTURER_SST, 0x26U, 0x42U, 0x400000UL);
    chip.locked_for_ever = true;
    mutex_created_at     = NULL;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
}

/*****************************************************************************************************/
/**
 * @brief An AT25DF, locked at every power up, is unlocked, its lock on the lock included.
 */
void test_at25df_is_unlocked_at_init(void)
{
    chip_new(KIND_AT25DF, SPIF_MANUFACTURER_ATMEL, 0x47U, 0x01U, 0x400000UL);
    chip.sprl = true;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_FALSE_MESSAGE(chip.locked, "the AT25DF was left locked");
    TEST_ASSERT_FALSE(chip.sprl);
    counts_clear();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x100U, pattern, 64U));
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x100U], 64U);

    /* Each status write takes the chip a while, which init sleeps through
       rather than spins: the wake up, then at least once for each of two. */
    chip_new(KIND_AT25DF, SPIF_MANUFACTURER_ATMEL, 0x47U, 0x01U, 0x400000UL);
    mutex_created_at = NULL;
    counts_clear();
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    TEST_ASSERT_TRUE_MESSAGE(sleeps >= 3, "spun on the status write instead of sleeping");

    /* With WP low the lock on the lock cannot be lifted. */
    chip_new(KIND_AT25DF, SPIF_MANUFACTURER_ATMEL, 0x47U, 0x01U, 0x400000UL);
    chip.sprl        = true;
    chip.wp_low      = true;
    mutex_created_at = NULL;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
}

/*****************************************************************************************************/
/**
 * @brief An ESMT F25L, locked at power up the same way, is unlocked by its status register too.
 */
void test_esmt_is_unlocked_at_init(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_AT25DF, SPIF_MANUFACTURER_ESMT, 0x20U,
                                                   0x16U, 0x400000UL));
    TEST_ASSERT_FALSE(chip.locked);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, readback));
}

/*****************************************************************************************************/
/**
 * @brief Micron reports a refused write in its flag status, and spif clears it and says so.
 */
void test_micron_reports_a_refused_write(void)
{
    chip_new(KIND_MICRON, SPIF_MANUFACTURER_MICRON, 0xBAU, CHIP_16MB, 0x1000000UL);
    chip.bp = 2U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, write_all(0U, pattern, 512U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, chip.refusals, "went on after a refused page");
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00U, chip.flags, "left the error set");
    TEST_ASSERT_FALSE_MESSAGE(chip.wel, "left the write enable set");

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, erase_sector(3U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip.flags);

    /* With the protection off, it works. */
    chip.bp = 0U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0U, pattern, 512U));
    TEST_ASSERT_EQUAL_MEMORY(pattern, chip_mem, 512U);
    TEST_ASSERT_TRUE_MESSAGE(chip.count[0x70] > 0, "did not wait on the flag status register");
}

/*****************************************************************************************************/
/**
 * @brief Spansion stays busy after a refused write until told, which spif does, and says so.
 */
void test_spansion_reports_a_refused_write(void)
{
    chip_new(KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, 0x20U, CHIP_16MB, 0x1000000UL);
    chip.has_4byte_mode = false;
    chip.id[3]          = 0x4DU;
    chip.id[4]          = 0x01U;
    chip.bp             = 7U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, write_all(0U, pattern, 300U));
    TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00U, chip.errors, "left the chip stuck busy");
    TEST_ASSERT_EQUAL_INT(1, chip.count[0x30]);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_PROTECTED, erase_sector(1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 16U));
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Erase sizes
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief A Spansion FL-S with 64 KB sectors erases those, and refuses 32 KB.
 */
void test_spansion_fl_s_erases_its_own_sectors(void)
{
    chip_new(KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, 0x20U, CHIP_16MB, 0x1000000UL);
    chip.has_4byte_mode = false;
    chip.id[3]          = 0x4DU;
    chip.id[4]          = 0x01U;
    chip.id[5]          = 0x80U;
    chip.big_sector     = KB64;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_EQUAL_HEX32(KB64, flash.sector_size);
    memset(chip_mem, 0x00, 4U * KB64);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(2U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[(2U * KB64) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[2U * KB64]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[(3U * KB64) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[3U * KB64]);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(3U));
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[3U * KB64]);
    TEST_ASSERT_EQUAL_INT(2, chip.count[0xD8]);

    counts_clear();
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_block32(0U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, chip.transfers, "tried to erase less than a sector");
}

/*****************************************************************************************************/
/**
 * @brief A Spansion with 256 KB sectors erases those, and refuses anything smaller.
 */
void test_spansion_256kb_sectors(void)
{
    chip_new(KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, 0x02U, CHIP_32MB, 0x2000000UL);
    chip.id[3]          = 0x4DU;
    chip.id[4]          = 0x00U;
    chip.id[5]          = 0x80U;
    chip.big_sector     = KB256;
    chip.has_4byte_mode = false;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_EQUAL_HEX32(KB256, flash.sector_size);
    memset(chip_mem, 0x00, 3U * KB256);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(1U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[KB256 - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[KB256]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[(2U * KB256) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[2U * KB256]);
    TEST_ASSERT_EQUAL_INT(1, chip.count[0xDC]);

    counts_clear();
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_block(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_block32(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, erase_sector(128U));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
}

/*****************************************************************************************************/
/**
 * @brief The older Spansion IDs tell their sector size the same way, and small chips have 64 KB.
 */
void test_older_spansion_ids_give_their_sector_size(void)
{
    static const struct
    {
        uint8_t  type;
        uint8_t  capacity;
        uint8_t  id3;
        uint8_t  id4;
        uint32_t size;
        uint32_t sector;
    } chips[] = {
        { 0x20U, 0x18U, 0x03U, 0x00U, 0x1000000UL, KB256 }, /* S25SL12800 */
        { 0x20U, 0x18U, 0x03U, 0x01U, 0x1000000UL, KB64  }, /* S25SL12801 */
        { 0x20U, 0x18U, 0x4DU, 0x00U, 0x1000000UL, KB256 }, /* S25FL129P0 */
        { 0x02U, 0x15U, 0x4DU, 0x00U, 0x400000UL,  KB64  }, /* S25FL032P  */
        { 0x02U, 0x16U, 0xFFU, 0xFFU, 0x800000UL,  KB64  }, /* S25SL064A  */
    };
    size_t i = 0U;

    for (i = 0U; i < (sizeof(chips) / sizeof(chips[0])); i++)
    {
        chip_new(KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, chips[i].type, chips[i].capacity,
                 chips[i].size);
        chip.has_4byte_mode = false;
        chip.id[3]          = chips[i].id3;
        chip.id[4]          = chips[i].id4;
        mutex_created_at    = NULL;

        TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
        TEST_ASSERT_EQUAL_HEX32(chips[i].sector, flash.sector_size);
    }
}

/*****************************************************************************************************/
/**
 * @brief The old M25P erases 64 KB sectors, and the M25P10 32 KB ones, two to a block.
 */
void test_an_old_m25p_erases_its_own_sectors(void)
{
    chip_new(KIND_M25P, SPIF_MANUFACTURER_MICRON, 0x20U, 0x17U, 0x800000UL);
    chip.big_sector = KB64;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_EQUAL_HEX32(KB64, flash.sector_size);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(5U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(6U));
    TEST_ASSERT_EQUAL_INT(2, chip.count[0xD8]);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_block32(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, readback));

    chip_new(KIND_M25P, SPIF_MANUFACTURER_MICRON, 0x20U, 0x11U, 0x20000UL);
    chip.big_sector  = KB32;
    mutex_created_at = NULL;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));
    counts_clear();

    TEST_ASSERT_EQUAL_HEX32(KB32, flash.sector_size);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block32(3U));
    TEST_ASSERT_EQUAL_INT(1, chip.count[0xD8]);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(1U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(3, chip.count[0xD8], "64 KB is two 32 KB sectors");

    /* The M25P128 has 256 KB sectors. */
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_M25P, SPIF_MANUFACTURER_MICRON, 0x20U,
                                                   0x18U, 0x1000000UL));
    TEST_ASSERT_EQUAL_HEX32(KB256, flash.sector_size);
}

/*****************************************************************************************************/
/**
 * @brief An SST26 erases its 32 KB and 64 KB blocks a sector at a time, and the chip at once.
 */
void test_sst26_erases_blocks_a_sector_at_a_time(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_SST26, SPIF_MANUFACTURER_SST, 0x26U,
                                                   0x42U, 0x400000UL));
    memset(chip_mem, 0x00, 4U * KB64);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block32(1U));
    TEST_ASSERT_EQUAL_INT(8, chip.count[0x20]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[KB32 - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[KB32]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[(2U * KB32) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[2U * KB32]);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(0U));
    TEST_ASSERT_EQUAL_INT(24, chip.count[0x20]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[KB64]);

    memset(chip_mem, 0x00, 16U);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0]);
}

/*****************************************************************************************************/
/**
 * @brief A stacked Winbond chip has each die asked whether it is busy, and is erased by blocks.
 */
void test_a_stacked_winbond_chip_is_asked_die_by_die(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, 0x21U));
    TEST_ASSERT_EQUAL_INT(2, chip.dice);

    /* Two pages into the second die: the second one is sent only once that
       die has stored the first, which die 0 cannot tell. */
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x4000000U, pattern, 512U));
    TEST_ASSERT_TRUE_MESSAGE(chip.count[0xC2] > 0, "never selected a die");

    /* Erased a block at a time, which every die takes, and far quicker here. */
    counts_clear();
    chip.block_us = 2000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
    TEST_ASSERT_EQUAL_INT(2048, chip.count[0xD8]);
    TEST_ASSERT_EQUAL_INT(0, chip.count[0xC7]);
}

/*****************************************************************************************************/
/**
 * @brief A stacked Micron chip waits on its flag status, and is erased by blocks too.
 */
void test_a_stacked_micron_chip_is_erased_by_blocks(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_MICRON, SPIF_MANUFACTURER_MICRON, 0xBAU,
                                                   0x22U, 0x10000000UL));
    chip.block_us = 1500U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
    TEST_ASSERT_EQUAL_INT(4096, chip.count[0xD8]);
    TEST_ASSERT_EQUAL_INT(0, chip.count[0xC7]);
}

/*****************************************************************************************************/
/**
 * @brief Each block of a stacked chip's erase gets a block's time, not a sector's.
 */
void test_a_stacked_chip_erase_gives_each_block_its_time(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_MICRON, SPIF_MANUFACTURER_MICRON, 0xBAU,
                                                   0x21U, 0x8000000UL));
    chip.block_us = (SPIF_TIMEOUT_SECTOR_MS + 20U) * 1000U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
    TEST_ASSERT_EQUAL_INT(2048, chip.count[0xD8]);
}

/*****************************************************************************************************/
/**
 * @brief A 64 MB chip is one die: it is erased with one command, and never asked die by die.
 */
void test_a_64mb_chip_is_one_die(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, 0x20U));
    TEST_ASSERT_EQUAL_INT(1, chip.dice);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(3U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
    TEST_ASSERT_EQUAL_INT(1, chip.count[0xC7]);
    TEST_ASSERT_EQUAL_INT(0, chip.count[0xC2]);
}

/*****************************************************************************************************/
/**
 * @brief A chip under 1 MB still gets time for a chip erase: a whole MB's.
 */
void test_a_small_chip_erase_gets_time(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, 0x11U));
    chip.chip_us = 1000000U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Read
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief A read returns what the chip holds, from any address, with a fast read.
 */
void test_a_read_returns_what_the_chip_holds(void)
{
    static const uint32_t addresses[] = { 0U, 1U, 255U, 4095U, 0x12345U, 0xFFFFF0U };
    static const uint32_t lengths[]   = { 1U, 7U, 300U, 5000U, 1U, 16U };
    size_t                i           = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    for (i = 0U; i < (sizeof(addresses) / sizeof(addresses[0])); i++)
    {
        memcpy(&chip_mem[addresses[i]], pattern, lengths[i]);
        memset(readback, 0, lengths[i]);

        TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(addresses[i], readback, lengths[i]));
        TEST_ASSERT_EQUAL_MEMORY(pattern, readback, lengths[i]);
    }

    TEST_ASSERT_TRUE_MESSAGE(chip.count[0x0B] > 0, "did not use the fast read");
}

/*****************************************************************************************************/
/**
 * @brief A read over 64 KB goes in pieces the HAL can count, all in one transaction.
 *
 * 2.3.2 handed the HAL the whole length, which it counts in 16 bits. Exactly
 * 64 KB is the worst case: counted in 16 bits it is 0.
 */
void test_a_read_over_64kb_goes_in_pieces(void)
{
    static const size_t lengths[] = { 0x10000U, 100000U };
    size_t              i         = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memcpy(&chip_mem[5U], pattern, 100000U);

    for (i = 0U; i < (sizeof(lengths) / sizeof(lengths[0])); i++)
    {
        memset(readback, 0, lengths[i]);
        chip.selects = 0;

        TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(5U, readback, lengths[i]));

        TEST_ASSERT_EQUAL_MEMORY(pattern, readback, lengths[i]);
        TEST_ASSERT_TRUE_MESSAGE(chip.largest_transfer <= 0xFFFFU, "more than the HAL can count");
        TEST_ASSERT_EQUAL_INT_MESSAGE(1, chip.selects, "the read was split into several commands");
    }
}

/*****************************************************************************************************/
/**
 * @brief Anything that runs past the end of the chip is refused, with no traffic.
 */
void test_past_the_end_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, read_all(flash.size - 1U, readback, 2U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, read_all(flash.size, readback, 1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, write_all(flash.size - 1U, pattern, 2U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, read_all(0xFFFFFFFFU, readback, 2U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, read_all(16U, readback, SIZE_MAX));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, write_all(16U, pattern, SIZE_MAX - 8U));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
}

/*****************************************************************************************************/
/**
 * @brief The last byte of the chip can be written and read.
 */
void test_the_last_byte_is_in_range(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(flash.size - 1U, pattern, 1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(flash.size - 1U, readback, 1U));
    TEST_ASSERT_EQUAL_UINT8(pattern[0], readback[0]);
}

/*****************************************************************************************************/
/**
 * @brief Nothing to do is done at once, and succeeds, even just past the last byte.
 */
void test_zero_bytes_does_nothing(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_read(&flash, 0U, readback, 0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_write(&flash, 0U, pattern, 0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_read(&flash, flash.size, readback, 0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_write(&flash, flash.size, pattern, 0U));
    TEST_ASSERT_FALSE_MESSAGE(spif_is_busy(&flash), "nothing should be running");
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mutex_takes, "nothing to do still took the mutex");
}

/*****************************************************************************************************/
/**
 * @brief A failed transfer is reported, and the chip and the mutex are given back.
 */
void test_a_failed_read_is_reported(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.fail_at   = 2;
    chip.fail_with = HAL_ERROR;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_SPI, read_all(0U, readback, 16U));

    counts_clear();
    chip.fail_at   = 1;
    chip.fail_with = HAL_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, read_all(0U, readback, 16U));
}

/*****************************************************************************************************/
/**
 * @brief A read moves its data the way SPIF_TRANSFER says, and its command never by DMA.
 */
void test_a_read_moves_its_data_as_configured(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memcpy(&chip_mem[100U], pattern, 70000U);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(100U, readback, 70000U));

    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 70000U);
    TEST_ASSERT_EQUAL_INT_MESSAGE(USES_DMA ? 3 : 0, chip.dma_transfers,
                                  "70000 bytes are three pieces by DMA, and none by polling");
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Write
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief A write lands where it was asked, split where the pages end.
 */
void test_a_write_is_split_at_every_page(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x1F0U, pattern, 600U));

    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x1F0U], 600U);
    TEST_ASSERT_EQUAL_INT(4, chip.programs);
    TEST_ASSERT_EQUAL_UINT32_MESSAGE(16U, chip.program_sizes[0], "did not stop at the first page end");
    TEST_ASSERT_EQUAL_UINT32(256U, chip.program_sizes[1]);
    TEST_ASSERT_EQUAL_UINT32(256U, chip.program_sizes[2]);
    TEST_ASSERT_EQUAL_UINT32(72U, chip.program_sizes[3]);
}

/*****************************************************************************************************/
/**
 * @brief A write only clears bits, as flash does, so flash not erased first keeps its zeros.
 */
void test_a_write_only_clears_bits(void)
{
    static const uint8_t ones[1] = { 0xF0U };

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip_mem[42U] = 0x0FU;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(42U, ones, 1U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[42U]);
}

/*****************************************************************************************************/
/**
 * @brief A write waits for each page before it sends the next.
 *
 * spif_wait() asks the chip, and sleeps through osal a millisecond at a time
 * between the questions.
 */
void test_a_write_waits_for_each_page(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0U, pattern, 1024U));

    TEST_ASSERT_EQUAL_INT(4, chip.programs);
    TEST_ASSERT_TRUE_MESSAGE(sleeps >= 4, "a page was not waited for");
    TEST_ASSERT_FALSE_MESSAGE(chip_busy(), "returned with the chip still busy");
}

/*****************************************************************************************************/
/**
 * @brief A write leaves the caller's data as it was.
 *
 * An issue against an older version found the source buffer changed after a write.
 */
void test_a_write_leaves_the_data_alone(void)
{
    uint8_t copy[600];

    memcpy(copy, pattern, sizeof(copy));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(3U, pattern, sizeof(copy)));
    TEST_ASSERT_EQUAL_MEMORY(copy, pattern, sizeof(copy));
}

/*****************************************************************************************************/
/**
 * @brief A write moves its data the way SPIF_TRANSFER says, and its commands never by DMA.
 */
void test_a_write_moves_its_data_as_configured(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0x2080U, pattern, 1000U));

    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x2080U], 1000U);
    TEST_ASSERT_EQUAL_INT(5, chip.programs);
    TEST_ASSERT_EQUAL_INT_MESSAGE(USES_DMA ? 5 : 0, chip.dma_transfers,
                                  "five pages are five pieces by DMA, and none by polling");
}

/*****************************************************************************************************/
/**
 * @brief A page that fails ends the write there, with no page after it sent.
 */
void test_a_failed_page_stops_the_write(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    /* The write enable of the first page. */
    chip.fail_at   = 1;
    chip.fail_with = HAL_ERROR;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_SPI, write_all(0U, pattern, 512U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, chip.transfers, "went on to the next page");
    TEST_ASSERT_EQUAL_INT(0, chip.programs);
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Erase
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief Each erase clears exactly its own sector or block, and waits until it is done.
 */
void test_each_erase_clears_exactly_its_own_range(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memset(chip_mem, 0x00, 0x40000U);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(3U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[(3U * KB4) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[3U * KB4]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[(4U * KB4) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[4U * KB4]);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block32(2U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[(2U * KB32) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[2U * KB32]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[(3U * KB32) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[3U * KB32]);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(2U));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[(2U * KB64) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[2U * KB64]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[(3U * KB64) - 1U]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[3U * KB64]);

    TEST_ASSERT_EQUAL_INT(3, chip.erases);
    TEST_ASSERT_FALSE_MESSAGE(chip_busy(), "returned with the chip still erasing");
}

/*****************************************************************************************************/
/**
 * @brief A sector or block past the last one is refused, with no traffic.
 */
void test_an_erase_past_the_end_is_refused(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(4095U));
    counts_clear();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, erase_sector(4096U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, erase_block32(512U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, erase_block(256U));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
}

/*****************************************************************************************************/
/**
 * @brief A chip erase clears everything.
 */
void test_a_chip_erase_clears_everything(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memset(chip_mem, 0x00, flash.size);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[flash.size - 1U]);
    TEST_ASSERT_EQUAL_INT(1, chip.count[0xC7]);
}

/*****************************************************************************************************/
/**
 * @brief The address macros turn an address into a page, sector or block number, and back.
 */
void test_the_address_macros_convert_both_ways(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_HEX32(0x123U, SPIF_ADDRESS_TO_PAGE(0x12345U));
    TEST_ASSERT_EQUAL_HEX32(0x12U, SPIF_ADDRESS_TO_SECTOR(&flash, 0x12345U));
    TEST_ASSERT_EQUAL_HEX32(0x2U, SPIF_ADDRESS_TO_BLOCK32(0x12345U));
    TEST_ASSERT_EQUAL_HEX32(0x1U, SPIF_ADDRESS_TO_BLOCK(0x12345U));

    TEST_ASSERT_EQUAL_HEX32(0x12300U, SPIF_PAGE_TO_ADDRESS(0x123U));
    TEST_ASSERT_EQUAL_HEX32(0x12000U, SPIF_SECTOR_TO_ADDRESS(&flash, 0x12U));
    TEST_ASSERT_EQUAL_HEX32(0x10000U, SPIF_BLOCK32_TO_ADDRESS(2U));
    TEST_ASSERT_EQUAL_HEX32(0x30000U, SPIF_BLOCK_TO_ADDRESS(3U));

    /* The sector holding an address is the one erased. */
    memset(chip_mem, 0x00, 0x5000U);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE,
                          erase_sector(SPIF_ADDRESS_TO_SECTOR(&flash, 0x3456U)));
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[0x2FFFU]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x3000U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x3FFFU]);
    TEST_ASSERT_EQUAL_HEX8(0x00U, chip_mem[0x4000U]);

    /* On a chip with 256 KB sectors, the sector ones follow the chip. */
    chip_new(KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, 0x20U, CHIP_16MB, 0x1000000UL);
    chip.has_4byte_mode = false;
    chip.id[3]          = 0x4DU;
    chip.id[4]          = 0x00U;
    chip.big_sector     = KB256;
    mutex_created_at    = NULL;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_init(&flash, &test_spi, &test_cs_port, CS_PIN));

    TEST_ASSERT_EQUAL_HEX32(0x0U, SPIF_ADDRESS_TO_SECTOR(&flash, 0x12345U));
    TEST_ASSERT_EQUAL_HEX32(0x2U, SPIF_ADDRESS_TO_SECTOR(&flash, 0x80000U));
    TEST_ASSERT_EQUAL_HEX32(0xC0000U, SPIF_SECTOR_TO_ADDRESS(&flash, 3U));
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Timeouts
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief Each step gets the time spif_config.h gives it.
 *
 * Each erase here takes just under its own time, and longer than a smaller one
 * gets, so a step given another step's time fails.
 */
void test_each_step_gets_its_own_time(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.page_us = (SPIF_TIMEOUT_PAGE_MS - 2U) * 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0U, pattern, 512U));

    chip.sector_us = (SPIF_TIMEOUT_SECTOR_MS - 10U) * 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(1U));

    chip.block32_us = (SPIF_TIMEOUT_BLOCK32_MS - 10U) * 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block32(1U));

    chip.block_us = (SPIF_TIMEOUT_BLOCK_MS - 10U) * 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_block(1U));

    chip.chip_us = ((16U * SPIF_TIMEOUT_CHIP_PER_MB_MS) - 1000U) * 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
}

/*****************************************************************************************************/
/**
 * @brief A chip erase gets more time on a bigger chip, a MB at a time.
 */
void test_a_chip_erase_gets_time_for_its_size(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_32MB));

    /* Longer than a 16 MB chip would get. */
    chip.chip_us = (24U * SPIF_TIMEOUT_CHIP_PER_MB_MS) * 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_chip());
}

/*****************************************************************************************************/
/**
 * @brief An erase that takes too long ends at its time.
 */
void test_a_slow_erase_times_out(void)
{
    uint32_t start = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.sector_us = (SPIF_TIMEOUT_SECTOR_MS + 100U) * 1000U;
    start          = HAL_GetTick();
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, erase_sector(1U));
    TEST_ASSERT_UINT32_WITHIN(3U, SPIF_TIMEOUT_SECTOR_MS, HAL_GetTick() - start);

    /* Let the chip finish before the checks at the end. */
    now_us += 200000U;
}

/*****************************************************************************************************/
/**
 * @brief A chip that never finishes a page ends the write at the page's time.
 */
void test_a_chip_that_never_finishes_times_out(void)
{
    uint32_t start = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.never_ready = true;
    start            = HAL_GetTick();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, write_all(0U, pattern, 512U));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, chip.programs, "sent a page while the chip was busy");
    TEST_ASSERT_UINT32_WITHIN(2U, SPIF_TIMEOUT_PAGE_MS, HAL_GetTick() - start);

    chip.never_ready = false;
    memset(chip.busy_until_us, 0, sizeof(chip.busy_until_us));
}

#if WAITS_FOR_EVER
/*****************************************************************************************************/
/**
 * @brief HAL_MAX_DELAY waits for ever: an erase still running 49.7 days on is not timed out.
 *
 * Counted down like any other time, HAL_MAX_DELAY runs out when 0xFFFFFFFF ms
 * have gone by. The clock is moved to just before that, and on past it.
 */
void test_hal_max_delay_outlasts_the_tick(void)
{
    uint32_t i = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.never_ready = true;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_erase_sector(&flash, 0U));

    /* On to the start of a millisecond, so the few microseconds each status
       read takes never carry the tick past the one being checked. */
    now_us       += 1000U - (now_us % 1000U);
    tick_jump_ms += (HAL_MAX_DELAY - 2U) - (HAL_GetTick() - flash.job_start);

    for (i = 0U; i < 5U; i++)
    {
        TEST_ASSERT_EQUAL_HEX32(HAL_MAX_DELAY - 2U + i, HAL_GetTick() - flash.job_start);
        TEST_ASSERT_TRUE_MESSAGE(spif_is_busy(&flash),
                                 "timed out, where HAL_MAX_DELAY waits for ever");
        osal_delay_ms(1U);
    }

    /* The chip finishes at last, and the erase ends well. */
    chip.never_ready = false;
    memset(chip.busy_until_us, 0, sizeof(chip.busy_until_us));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
}

/*****************************************************************************************************/
/**
 * @brief HAL_MAX_DELAY for each MB of a chip erase is not multiplied into a time.
 *
 * 16 times 0xFFFFFFFF wraps round to 0xFFFFFFF0, which is a time: 49.7 days.
 */
void test_hal_max_delay_per_mb_is_not_multiplied(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_erase_chip(&flash));
    TEST_ASSERT_EQUAL_HEX32(HAL_MAX_DELAY, flash.job_timeout);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
}
#endif /* WAITS_FOR_EVER */

/*****************************************************************************************************/
/**
 * @brief The HAL is given the transfer time spif_config.h sets.
 */
void test_the_hal_gets_the_transfer_time(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 16U));
    TEST_ASSERT_EQUAL_UINT32(SPIF_TIMEOUT_TRANSFER_MS, chip.last_timeout);
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Jobs, and DMA
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief With nothing running, spif_is_busy() says no, and spif_wait() returns at once.
 */
void test_nothing_running_is_not_busy(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_FALSE(spif_is_busy(&flash));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
    TEST_ASSERT_EQUAL_INT(0, sleeps);
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
}

/*****************************************************************************************************/
/**
 * @brief An erase returns at once, and spif_is_busy() follows it while the caller does other work.
 */
void test_an_erase_returns_at_once(void)
{
    uint32_t start = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memset(chip_mem, 0x00, 16U);
    start = now_us;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_erase_chip(&flash));
    TEST_ASSERT_TRUE_MESSAGE((now_us - start) < 1000U, "the call waited for the erase");
    TEST_ASSERT_EQUAL_INT(0, sleeps);
    TEST_ASSERT_TRUE(spif_is_busy(&flash));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mutex_held, "gave the mutex back while erasing");

    /* The caller's state machine does something else for a while. */
    now_us += CHIP_US / 2U;
    TEST_ASSERT_TRUE(spif_is_busy(&flash));
    now_us += CHIP_US;
    TEST_ASSERT_FALSE(spif_is_busy(&flash));
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0]);

    /* Ended: spif_wait() says how, at once. */
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sleeps, "spif_is_busy() or spif_wait() slept on an ended job");
}

/*****************************************************************************************************/
/**
 * @brief A write returns once its first page is out, and spif_is_busy() sends the others.
 */
void test_a_write_returns_after_its_first_page(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_write(&flash, 0U, pattern, 768U));

    /* By polling the first page is being stored. With DMA it is still on
       its way, and stored once spif_is_busy() lets the chip go. */
    TEST_ASSERT_EQUAL_INT(USES_DMA ? 0 : 1, chip.programs);

    while (spif_is_busy(&flash))
    {
        now_us += 100U;
    }

    TEST_ASSERT_EQUAL_INT(3, chip.programs);
    TEST_ASSERT_EQUAL_MEMORY(pattern, chip_mem, 768U);
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sleeps, "spif_is_busy() slept");
}

/*****************************************************************************************************/
/**
 * @brief While a job runs every other call is refused as busy, with no traffic, and the job goes on.
 */
void test_a_call_while_a_job_runs_is_refused(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];
    int     transfers = 0;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_erase_sector(&flash, 1U));
    transfers = chip.transfers;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_read(&flash, 0U, readback, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_write(&flash, 0U, pattern, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_erase_sector(&flash, 2U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_erase_block32(&flash, 2U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_erase_block(&flash, 2U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_erase_chip(&flash));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_INT(transfers, chip.transfers);

    TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, spif_wait(&flash),
                                  "a refused call took over the result of the job");
    TEST_ASSERT_EQUAL_INT(1, chip.erases);
}

/*****************************************************************************************************/
/**
 * @brief spif_wait() reports a call refused at once too, so simple code needs only it.
 */
void test_wait_reports_a_refused_call(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    /* Each refused call comes after one that worked, so its result cannot
       be left over from the call before. */
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, spif_write(&flash, flash.size, pattern, 1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, spif_wait(&flash));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, spif_erase_sector(&flash, 4096U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, spif_wait(&flash));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0U, pattern, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, spif_read(&flash, flash.size, readback, 1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_RANGE, spif_wait(&flash));
    TEST_ASSERT_EQUAL_INT(0, chip.erases);
}

/*****************************************************************************************************/
/**
 * @brief Every command, on every kind of chip, behaves as a job should.
 *
 * Each write and erase, and with DMA each read, returns at once without
 * sleeping. spif_is_busy() then says true, holds the chip for the job, and
 * refuses other calls, until the chip has done it. Then it says false,
 * spif_wait() returns the result at once, and the chip holds what it should.
 * A read by polling is done when it returns.
 */
void test_every_command_on_every_chip(void)
{
    static const struct
    {
        chip_kind_t kind;
        uint8_t     maker;
        uint8_t     type;
        uint8_t     capacity;
        uint32_t    size;
        uint8_t     id3;
        uint8_t     id4;
        uint32_t    big_sector;
        bool        has_block32;
        const char  *name;
    } chips[] = {
        { KIND_WINBOND,  SPIF_MANUFACTURER_WINBOND,  0x40U, 0x18U, 0x1000000UL, 0xFFU, 0xFFU, KB64,
          true,  "W25Q128"     },
        { KIND_MICRON,   SPIF_MANUFACTURER_MICRON,   0xBAU, 0x18U, 0x1000000UL, 0xFFU, 0xFFU, KB64,
          true,  "MT25QL128"   },
        { KIND_SPANSION, SPIF_MANUFACTURER_SPANSION, 0x20U, 0x18U, 0x1000000UL, 0x4DU, 0x01U, KB64,
          false, "S25FL128S"   },
        { KIND_SST26,    SPIF_MANUFACTURER_SST,      0x26U, 0x42U, 0x400000UL,  0xFFU, 0xFFU, KB64,
          true,  "SST26VF032B" },
        { KIND_AT25DF,   SPIF_MANUFACTURER_ATMEL,    0x47U, 0x01U, 0x400000UL,  0xFFU, 0xFFU, KB64,
          true,  "AT25DF321A"  },
        { KIND_M25P,     SPIF_MANUFACTURER_MICRON,   0x20U, 0x11U, 0x20000UL,   0xFFU, 0xFFU, KB32,
          true,  "M25P10"      },
        { KIND_WINBOND,  SPIF_MANUFACTURER_WINBOND,  0x40U, 0x21U, 0x8000000UL, 0xFFU, 0xFFU, KB64,
          true,  "W25Q01"      },
    };
    uint8_t    id[SPIF_UNIQUE_ID_SIZE];
    size_t     i         = 0U;
    int        command   = 0;
    spif_err_t err       = SPIF_ERR_NONE;
    uint32_t   address   = 0U;
    uint32_t   len       = 0U;
    uint32_t   start     = 0U;
    int        transfers = 0;
    long       rounds    = 0;

    for (i = 0U; i < (sizeof(chips) / sizeof(chips[0])); i++)
    {
        chip_new(chips[i].kind, chips[i].maker, chips[i].type, chips[i].capacity, chips[i].size);
        chip.id[3]          = chips[i].id3;
        chip.id[4]          = chips[i].id4;
        chip.big_sector     = chips[i].big_sector;
        chip.has_4byte_mode = (chips[i].maker != SPIF_MANUFACTURER_SPANSION);
        chip.block_us       = 2000U;
        chip.chip_us        = 2000000U;
        mutex_created_at    = NULL;
        TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE,
                                      spif_init(&flash, &test_spi, &test_cs_port, CS_PIN),
                                      chips[i].name);

        /* 0 write, 1 sector, 2 32 KB block, 3 64 KB block, 4 chip, 5 read. */
        for (command = 0; command < 6; command++)
        {
            counts_clear();
            address = 0U;
            len     = 0U;

            switch (command)
            {
                case 0:
                    /* Two pages, across a page end. */
                    address = 0x1F0U;
                    len     = 300U;
                    break;

                case 1:
                    address = flash.sector_size;
                    len     = flash.sector_size;
                    break;

                case 2:
                    address = (uint32_t)KB32;
                    len     = (uint32_t)KB32;
                    break;

                case 3:
                    address = (uint32_t)KB64;
                    len     = (uint32_t)KB64;
                    break;

                case 4:
                    len = 64U;
                    break;

                default:
                    address = 0x400U;
                    len     = 5000U;
                    memcpy(&chip_mem[address], pattern, len);
                    break;
            }

            /* What an erase must clear, with a byte either side it must not. */
            if ((command >= 1) && (command <= 4))
            {
                memset(&chip_mem[(address != 0U) ? (address - 1U) : 0U], 0x00, len + 2U);
            }

            start = now_us;

            switch (command)
            {
                case 0:
                    err = spif_write(&flash, address, pattern, len);
                    break;

                case 1:
                    err = spif_erase_sector(&flash, 1U);
                    break;

                case 2:
                    err = spif_erase_block32(&flash, 1U);
                    break;

                case 3:
                    err = spif_erase_block(&flash, 1U);
                    break;

                case 4:
                    err = spif_erase_chip(&flash);
                    break;

                default:
                    err = spif_read(&flash, address, readback, len);
                    break;
            }

            /* A chip whose sectors are bigger than 32 KB refuses that erase, at
               once and with no traffic. */
            if ((command == 2) && !chips[i].has_block32)
            {
                TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_INVALID, err, chips[i].name);
                TEST_ASSERT_EQUAL_INT(0, chip.transfers);
                TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_wait(&flash));
                continue;
            }

            TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, err, chips[i].name);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, sleeps, "a command slept");

            if ((command < 5) || USES_DMA)
            {
                /* A job: back at once, running, holding the chip, refusing others. */
                TEST_ASSERT_TRUE_MESSAGE((now_us - start) < 1000U, "the call waited for its job");
                TEST_ASSERT_TRUE_MESSAGE(spif_is_busy(&flash), chips[i].name);
                TEST_ASSERT_EQUAL_INT(1, mutex_held);

                transfers = chip.transfers;
                TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_read(&flash, 0U, id, 1U));
                TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_write(&flash, 0U, id, 1U));
                TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_erase_sector(&flash, 0U));
                TEST_ASSERT_EQUAL_INT(SPIF_ERR_BUSY, spif_erase_chip(&flash));
                TEST_ASSERT_EQUAL_INT(transfers, chip.transfers);

                /* The caller does other work, asking now and then. */
                rounds = 0;

                while (spif_is_busy(&flash) && (rounds < 100000000L))
                {
                    now_us += 500U;
                    rounds++;
                }

                TEST_ASSERT_FALSE_MESSAGE(chip_busy(), chips[i].name);
            }

            TEST_ASSERT_FALSE(spif_is_busy(&flash));
            TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, spif_wait(&flash), chips[i].name);
            TEST_ASSERT_EQUAL_INT_MESSAGE(0, sleeps, "spif_is_busy() or spif_wait() slept");
            TEST_ASSERT_EQUAL_INT(0, mutex_held);
            TEST_ASSERT_FALSE(chip.cs_low);

            /* And the chip holds what it should. */
            if (command == 0)
            {
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(pattern, &chip_mem[address], len, chips[i].name);
            }
            else if (command == 5)
            {
                TEST_ASSERT_EQUAL_MEMORY_MESSAGE(pattern, readback, len, chips[i].name);
            }
            else if (command == 4)
            {
                TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0]);
                TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[63]);
            }
            else
            {
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00U, chip_mem[address - 1U], chips[i].name);
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFFU, chip_mem[address], chips[i].name);
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0xFFU, chip_mem[address + len - 1U], chips[i].name);
                TEST_ASSERT_EQUAL_HEX8_MESSAGE(0x00U, chip_mem[address + len], chips[i].name);
            }
        }
    }
}

/*****************************************************************************************************/
/**
 * @brief A state machine runs every command, one after another, and never waits.
 *
 * As a user's main loop would: start a job, go on with other work, and ask
 * spif_is_busy() each time round. Only once it says false does spif_wait()
 * fetch the result, which it then returns at once.
 */
void test_a_state_machine_runs_every_command(void)
{
    uint8_t    id[SPIF_UNIQUE_ID_SIZE];
    int        step    = 0;
    bool       running = false;
    int        other   = 0;
    long       rounds  = 0;
    spif_err_t err     = SPIF_ERR_NONE;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memset(chip_mem, 0x00, 0x20000U);
    memset(id, 0, sizeof(id));

    /* 0 erase the chip, 1 write in sector 1, 2 erase it, 3 write in 32 KB
       block 1, 4 erase it, 5 write in 64 KB block 1, 6 erase it, 7 write at
       0x100, 8 read it back, 9 the unique ID, 10 done. */
    while ((step < 10) && (rounds < 100000000L))
    {
        rounds++;

        /* The rest of the main loop. */
        now_us += 200U;

        if (running && spif_is_busy(&flash))
        {
            other++;
        }
        else if (running)
        {
            running = false;
            err     = spif_wait(&flash);
            TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, err, "a step failed");
            step++;
        }
        else
        {
            switch (step)
            {
                case 0:
                    err = spif_erase_chip(&flash);
                    break;

                case 1:
                    err = spif_write(&flash, 0x1000U, pattern, 64U);
                    break;

                case 2:
                    err = spif_erase_sector(&flash, SPIF_ADDRESS_TO_SECTOR(&flash, 0x1000U));
                    break;

                case 3:
                    err = spif_write(&flash, 0x8000U, pattern, 64U);
                    break;

                case 4:
                    err = spif_erase_block32(&flash, SPIF_ADDRESS_TO_BLOCK32(0x8000U));
                    break;

                case 5:
                    err = spif_write(&flash, 0x10000U, pattern, 64U);
                    break;

                case 6:
                    err = spif_erase_block(&flash, SPIF_ADDRESS_TO_BLOCK(0x10000U));
                    break;

                case 7:
                    err = spif_write(&flash, 0x100U, pattern, 300U);
                    break;

                case 8:
                    err = spif_read(&flash, 0x100U, readback, 300U);
                    break;

                default:
                    err = spif_unique_id(&flash, id);
                    break;
            }

            TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, err, "a step was refused");
            running = true;
        }
    }

    TEST_ASSERT_EQUAL_INT_MESSAGE(10, step, "the state machine did not get through");
    TEST_ASSERT_TRUE_MESSAGE(other > 1000, "no other work got done while the jobs ran");
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, sleeps, "something slept in a state machine");

    /* Each write was undone by its erase, and the last one read back. */
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x1000U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x8000U]);
    TEST_ASSERT_EQUAL_HEX8(0xFFU, chip_mem[0x10000U]);
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x100U], 300U);
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 300U);
    TEST_ASSERT_EQUAL_MEMORY(factory_id, id, sizeof(id));
}

#if USES_DMA

/*****************************************************************************************************/
/**
 * @brief A DMA read returns once it has started, and spif_is_busy() says when it is done.
 */
void test_a_dma_read_returns_at_once(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memcpy(&chip_mem[7U], pattern, 4096U);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_read(&flash, 7U, readback, 4096U));
    TEST_ASSERT_TRUE_MESSAGE(chip.cs_low, "let the chip go while the read was running");
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, mutex_held, "gave the mutex back while the read was running");
    TEST_ASSERT_TRUE(spif_is_busy(&flash));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
    TEST_ASSERT_EQUAL_MEMORY(pattern, readback, 4096U);
    TEST_ASSERT_FALSE(spif_is_busy(&flash));
}

/*****************************************************************************************************/
/**
 * @brief A DMA write sends each page only once the chip has stored the one before.
 */
void test_a_dma_write_goes_a_page_at_a_time(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_write(&flash, 0x80U, pattern, 384U));
    TEST_ASSERT_EQUAL_INT(1, chip.dma_transfers);

    /* The first page out, then being stored. */
    now_us += 200U;
    TEST_ASSERT_TRUE(spif_is_busy(&flash));
    TEST_ASSERT_FALSE_MESSAGE(chip.cs_low, "kept the chip selected while it stored the page");
    TEST_ASSERT_TRUE(spif_is_busy(&flash));
    TEST_ASSERT_EQUAL_INT_MESSAGE(1, chip.dma_transfers, "sent a page before the last was stored");

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_wait(&flash));
    TEST_ASSERT_EQUAL_INT(2, chip.dma_transfers);
    TEST_ASSERT_EQUAL_MEMORY(pattern, &chip_mem[0x80U], 384U);
}

/*****************************************************************************************************/
/**
 * @brief A DMA transfer that never finishes is stopped at its time.
 */
void test_a_stuck_dma_is_stopped_at_its_time(void)
{
    uint32_t start = 0U;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.dma_stall = true;
    start          = HAL_GetTick();

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, read_all(0U, readback, 1000U));
    TEST_ASSERT_EQUAL_INT(1, chip.dma_aborts);
    TEST_ASSERT_UINT32_WITHIN(2U, SPIF_TIMEOUT_TRANSFER_MS, HAL_GetTick() - start);

    /* A page stuck on its way out gets a transfer's time too, not a page's. */
    start = HAL_GetTick();
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, write_all(0U, pattern, 16U));
    TEST_ASSERT_EQUAL_INT(2, chip.dma_aborts);
    TEST_ASSERT_UINT32_WITHIN(2U, SPIF_TIMEOUT_TRANSFER_MS, HAL_GetTick() - start);

    /* The chip and the SPI are free again, once the chip has stored what part
       of the page reached it. */
    chip.dma_stall = false;
    now_us        += 1000U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 16U));
}

/*****************************************************************************************************/
/**
 * @brief A DMA transfer that ends in an error is reported.
 */
void test_a_dma_error_is_reported(void)
{
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    chip.dma_error = HAL_SPI_ERROR_DMA;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_SPI, read_all(0U, readback, 64U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_SPI, write_all(0U, pattern, 64U));
}

#endif /* USES_DMA */

/*
 * ----------------------------------------------------------------------------------------------------
 * Unique ID
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief The unique ID is read after four dummy bytes, or five with 4 byte addresses.
 */
void test_the_unique_id_is_read(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));
    memset(id, 0, sizeof(id));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_MEMORY(factory_id, id, sizeof(id));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_32MB));
    memset(id, 0, sizeof(id));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_MEMORY_MESSAGE(factory_id, id, sizeof(id), "wrong in 4 byte address mode");
}

/*****************************************************************************************************/
/**
 * @brief Each maker's ID is read its own way.
 */
void test_each_maker_gives_its_id_its_own_way(void)
{
    static const struct
    {
        chip_kind_t kind;
        uint8_t     maker;
        uint8_t     type;
        uint8_t     capacity;
        uint32_t    size;
    } chips[] = {
        { KIND_WINBOND,  SPIF_MANUFACTURER_GIGADEVICE, 0x40U, 0x18U, 0x1000000UL },
        { KIND_WINBOND,  SPIF_MANUFACTURER_ISSI,       0x60U, 0x18U, 0x1000000UL },
        { KIND_WINBOND,  SPIF_MANUFACTURER_XMC,        0x70U, 0x18U, 0x1000000UL },
        { KIND_WINBOND,  SPIF_MANUFACTURER_PUYA,       0x60U, 0x16U, 0x400000UL  },
        { KIND_WINBOND,  SPIF_MANUFACTURER_ZBIT,       0x40U, 0x16U, 0x400000UL  },
        { KIND_WINBOND,  SPIF_MANUFACTURER_BOYA,       0x40U, 0x16U, 0x400000UL  },
        { KIND_WINBOND,  SPIF_MANUFACTURER_ATMEL,      0x87U, 0x01U, 0x400000UL  },
        { KIND_WINBOND,  SPIF_MANUFACTURER_SPANSION,   0x40U, 0x17U, 0x800000UL  },
        { KIND_SST26,    SPIF_MANUFACTURER_SST,        0x26U, 0x43U, 0x800000UL  },
        { KIND_AT25DF,   SPIF_MANUFACTURER_ATMEL,      0x47U, 0x01U, 0x400000UL  },
        { KIND_SPANSION, SPIF_MANUFACTURER_SPANSION,   0x02U, 0x19U, 0x2000000UL },
    };
    uint8_t id[SPIF_UNIQUE_ID_SIZE];
    size_t  i = 0U;

    for (i = 0U; i < (sizeof(chips) / sizeof(chips[0])); i++)
    {
        TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(chips[i].kind, chips[i].maker, chips[i].type,
                                                       chips[i].capacity, chips[i].size));

        memset(id, 0, sizeof(id));
        TEST_ASSERT_EQUAL_INT_MESSAGE(SPIF_ERR_NONE, spif_unique_id(&flash, id),
                                      "a known ID was refused");
        TEST_ASSERT_EQUAL_MEMORY(factory_id, id, sizeof(id));
    }
}

/*****************************************************************************************************/
/**
 * @brief A chip with no unique ID, or one not known, is refused with no traffic.
 */
void test_a_chip_without_an_id_is_refused(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_NO_ID, SPIF_MANUFACTURER_MACRONIX, 0x20U,
                                                   CHIP_16MB, 0x1000000UL));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_MICRON, SPIF_MANUFACTURER_MICRON, 0xBAU,
                                                   CHIP_16MB, 0x1000000UL));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, id));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_NO_ID, SPIF_MANUFACTURER_EON, 0x30U,
                                                   0x16U, 0x400000UL));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, id));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_kind(KIND_NO_ID, SPIF_MANUFACTURER_ATMEL, 0x42U,
                                                   0x16U, 0x400000UL));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
    TEST_ASSERT_EQUAL_INT(0, mutex_takes);
}

/*
 * ----------------------------------------------------------------------------------------------------
 * Arguments and the mutex
 * ----------------------------------------------------------------------------------------------------
 */

/*****************************************************************************************************/
/**
 * @brief A NULL pointer is stopped by assert_param before anything is touched.
 */
void test_null_pointers_are_caught_by_assert(void)
{
    int which = 0;

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    for (which = 0; which < NULL_CALLS; which++)
    {
        TEST_ASSERT_TRUE_MESSAGE(assert_stops(which), "a NULL got past assert_param");
    }

    TEST_ASSERT_EQUAL_INT(NULL_CALLS, asserts);
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
    TEST_ASSERT_EQUAL_INT_MESSAGE(0, mutex_takes, "a NULL call took the mutex");
}

/*****************************************************************************************************/
/**
 * @brief A handle spif_init() never accepted is refused by every call, with no traffic.
 */
void test_a_handle_without_init_is_refused(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_read(&flash, 0U, readback, 1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_write(&flash, 0U, pattern, 1U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_sector(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_block32(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_block(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, erase_chip());
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_INVALID, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
    TEST_ASSERT_EQUAL_INT(0, mutex_takes);
}

/*****************************************************************************************************/
/**
 * @brief A chip another thread keeps too long ends in a timeout, for every call, with no traffic.
 */
void test_a_mutex_held_elsewhere_times_out(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    lock_result = OSAL_ERR_TIMEOUT;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, read_all(0U, readback, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, write_all(0U, pattern, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, erase_sector(0U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_TIMEOUT, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
}

/*****************************************************************************************************/
/**
 * @brief A mutex the RTOS refuses outright is reported, for every call, with no traffic.
 */
void test_a_refused_mutex_is_reported(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    lock_result = OSAL_ERR_MUTEX;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_MUTEX, read_all(0U, readback, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_MUTEX, write_all(0U, pattern, 4U));
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_MUTEX, erase_chip());
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_MUTEX, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_INT(0, chip.transfers);
}

/*****************************************************************************************************/
/**
 * @brief Every call waits for the mutex as long as spif_config.h says, and gives it back.
 */
void test_the_mutex_wait_is_the_configured_one(void)
{
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, init_chip(SPIF_MANUFACTURER_WINBOND, CHIP_16MB));

    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, read_all(0U, readback, 4U));
    TEST_ASSERT_EQUAL_UINT32(SPIF_TIMEOUT_MUTEX_MS, mutex_last_wait);
    mutex_last_wait = 0U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, write_all(0U, pattern, 4U));
    TEST_ASSERT_EQUAL_UINT32(SPIF_TIMEOUT_MUTEX_MS, mutex_last_wait);
    mutex_last_wait = 0U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, erase_sector(1U));
    TEST_ASSERT_EQUAL_UINT32(SPIF_TIMEOUT_MUTEX_MS, mutex_last_wait);
    mutex_last_wait = 0U;
    TEST_ASSERT_EQUAL_INT(SPIF_ERR_NONE, spif_unique_id(&flash, id));
    TEST_ASSERT_EQUAL_UINT32(SPIF_TIMEOUT_MUTEX_MS, mutex_last_wait);

    TEST_ASSERT_EQUAL_INT(4, mutex_takes);
    TEST_ASSERT_EQUAL_INT_MESSAGE(4, mutex_gives, "a call kept the mutex");
}

/*****************************************************************************************************/
/**
 * @brief Run every test.
 *
 * @return 0 when every test passed.
 */
int main(void)
{
    UNITY_BEGIN();

#if WAITS_FOR_EVER
    /* Only what HAL_MAX_DELAY changes. Every other test expects the erase
       times spif_config.h ships, and a chip that never finishes would never
       let one go. */
    RUN_TEST(test_hal_max_delay_outlasts_the_tick);
    RUN_TEST(test_hal_max_delay_per_mb_is_not_multiplied);
#else
    RUN_TEST(test_init_reads_the_size_from_the_chip);
    RUN_TEST(test_other_size_codes_are_read);
    RUN_TEST(test_a_chip_not_known_is_refused);
    RUN_TEST(test_no_chip_is_reported);
    RUN_TEST(test_an_spi_failure_in_init_is_reported);
    RUN_TEST(test_init_lets_go_of_a_chip_left_selected);
    RUN_TEST(test_init_waits_for_power_up);
    RUN_TEST(test_init_wakes_the_chip_first);
    RUN_TEST(test_up_to_16mb_uses_3_byte_addresses);
    RUN_TEST(test_over_16mb_is_switched_to_4_byte_addresses);
    RUN_TEST(test_micron_gets_a_write_enable_before_the_switch);
    RUN_TEST(test_spansion_uses_its_own_4_byte_commands);
    RUN_TEST(test_init_makes_one_mutex);
    RUN_TEST(test_a_mutex_that_cannot_be_made_is_reported);
    RUN_TEST(test_a_failed_init_leaves_the_handle_refused);
    RUN_TEST(test_a_handle_full_of_rubbish_works_after_init);

    RUN_TEST(test_write_protection_is_reported);
    RUN_TEST(test_sst26_is_unlocked_at_init);
    RUN_TEST(test_at25df_is_unlocked_at_init);
    RUN_TEST(test_esmt_is_unlocked_at_init);
    RUN_TEST(test_micron_reports_a_refused_write);
    RUN_TEST(test_spansion_reports_a_refused_write);

    RUN_TEST(test_spansion_fl_s_erases_its_own_sectors);
    RUN_TEST(test_spansion_256kb_sectors);
    RUN_TEST(test_older_spansion_ids_give_their_sector_size);
    RUN_TEST(test_an_old_m25p_erases_its_own_sectors);
    RUN_TEST(test_sst26_erases_blocks_a_sector_at_a_time);
    RUN_TEST(test_a_stacked_winbond_chip_is_asked_die_by_die);
    RUN_TEST(test_a_stacked_micron_chip_is_erased_by_blocks);
    RUN_TEST(test_a_stacked_chip_erase_gives_each_block_its_time);
    RUN_TEST(test_a_64mb_chip_is_one_die);
    RUN_TEST(test_a_small_chip_erase_gets_time);

    RUN_TEST(test_a_read_returns_what_the_chip_holds);
    RUN_TEST(test_a_read_over_64kb_goes_in_pieces);
    RUN_TEST(test_past_the_end_is_refused);
    RUN_TEST(test_the_last_byte_is_in_range);
    RUN_TEST(test_zero_bytes_does_nothing);
    RUN_TEST(test_a_failed_read_is_reported);
    RUN_TEST(test_a_read_moves_its_data_as_configured);

    RUN_TEST(test_a_write_is_split_at_every_page);
    RUN_TEST(test_a_write_only_clears_bits);
    RUN_TEST(test_a_write_waits_for_each_page);
    RUN_TEST(test_a_write_leaves_the_data_alone);
    RUN_TEST(test_a_write_moves_its_data_as_configured);
    RUN_TEST(test_a_failed_page_stops_the_write);

    RUN_TEST(test_each_erase_clears_exactly_its_own_range);
    RUN_TEST(test_an_erase_past_the_end_is_refused);
    RUN_TEST(test_a_chip_erase_clears_everything);
    RUN_TEST(test_the_address_macros_convert_both_ways);

    RUN_TEST(test_each_step_gets_its_own_time);
    RUN_TEST(test_a_chip_erase_gets_time_for_its_size);
    RUN_TEST(test_a_slow_erase_times_out);
    RUN_TEST(test_a_chip_that_never_finishes_times_out);
    RUN_TEST(test_the_hal_gets_the_transfer_time);

    RUN_TEST(test_nothing_running_is_not_busy);
    RUN_TEST(test_an_erase_returns_at_once);
    RUN_TEST(test_a_write_returns_after_its_first_page);
    RUN_TEST(test_a_call_while_a_job_runs_is_refused);
    RUN_TEST(test_wait_reports_a_refused_call);
    RUN_TEST(test_every_command_on_every_chip);
    RUN_TEST(test_a_state_machine_runs_every_command);
#if USES_DMA
    RUN_TEST(test_a_dma_read_returns_at_once);
    RUN_TEST(test_a_dma_write_goes_a_page_at_a_time);
    RUN_TEST(test_a_stuck_dma_is_stopped_at_its_time);
    RUN_TEST(test_a_dma_error_is_reported);
#endif

    RUN_TEST(test_the_unique_id_is_read);
    RUN_TEST(test_each_maker_gives_its_id_its_own_way);
    RUN_TEST(test_a_chip_without_an_id_is_refused);

    RUN_TEST(test_null_pointers_are_caught_by_assert);
    RUN_TEST(test_a_handle_without_init_is_refused);
    RUN_TEST(test_a_mutex_held_elsewhere_times_out);
    RUN_TEST(test_a_refused_mutex_is_reported);
    RUN_TEST(test_the_mutex_wait_is_the_configured_one);
#endif

    return UNITY_END();
}

/*
 * ****************************************************************************************************
 * Private function implementations
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief The size a Winbond style capacity code stands for.
 *
 * @param[in] capacity  JEDEC capacity code.
 * @return Bytes, or 0 for a code the driver should refuse.
 */
static uint32_t chip_size_of(uint8_t capacity)
{
    uint32_t size = 0U;

    if ((capacity >= 0x11U) && (capacity <= 0x1CU))
    {
        size = (uint32_t)1U << capacity;
    }
    else if ((capacity >= 0x20U) && (capacity <= 0x22U))
    {
        size = (uint32_t)(DIE_BYTES << (capacity - 0x20U));
    }
    else
    {
        /* The driver should refuse it, so its size never matters. */
    }

    return size;
}

/*****************************************************************************************************/
/**
 * @brief Put a new, erased chip of one kind on the bus.
 *
 * @param[in] kind      Which maker's ways it follows.
 * @param[in] maker     JEDEC maker.
 * @param[in] type      JEDEC memory type.
 * @param[in] capacity  JEDEC capacity code.
 * @param[in] size      Bytes.
 */
static void chip_new(chip_kind_t kind, uint8_t maker, uint8_t type, uint8_t capacity,
                     uint32_t size)
{
    memset(&chip, 0, sizeof(chip));
    memset(chip_mem, 0xFF, (size < CHIP_MAX_BYTES) ? size : CHIP_MAX_BYTES);

    chip.kind            = kind;
    chip.size            = (size != 0U) ? size : 0x100000U;
    chip.id[0]           = maker;
    chip.id[1]           = type;
    chip.id[2]           = capacity;
    chip.id[3]           = 0xFFU;
    chip.id[4]           = 0xFFU;
    chip.id[5]           = 0xFFU;
    chip.big_sector      = KB64;
    chip.dice            = ((maker == SPIF_MANUFACTURER_WINBOND) && (size > DIE_BYTES))
                               ? (int)(size / DIE_BYTES) : 1;
    chip.present         = true;
    chip.has_4b_commands = true;
    chip.has_4byte_mode  = true;
    chip.mode_needs_wren = (kind == KIND_MICRON);
    chip.locked          = (kind == KIND_SST26) || (kind == KIND_AT25DF);
    chip.page_us         = PAGE_US;
    chip.sector_us       = SECTOR_US;
    chip.block32_us      = BLOCK32_US;
    chip.block_us        = BLOCK_US;
    chip.chip_us         = CHIP_US;
    chip.fault           = "none";

    memcpy(chip.unique_id, factory_id, sizeof(factory_id));
    memset(chip.otp, 0x5A, sizeof(chip.otp));

    /* Spansion keeps its ID at the start of the OTP area, the AT25DF in its
       second half. */
    memcpy(&chip.otp[(kind == KIND_AT25DF) ? 64U : 0U], factory_id, sizeof(factory_id));
}

/*****************************************************************************************************/
/**
 * @brief Put a new, erased Winbond style chip on the bus, its size from the capacity code.
 *
 * @param[in] manufacturer  JEDEC maker.
 * @param[in] capacity      JEDEC capacity code.
 */
static void chip_reset(uint8_t manufacturer, uint8_t capacity)
{
    chip_new(KIND_WINBOND, manufacturer, 0x40U, capacity, chip_size_of(capacity));
}

/*****************************************************************************************************/
/**
 * @brief Forget what has happened so far, so a test sees only its own traffic.
 */
static void counts_clear(void)
{
    chip.transfers        = 0;
    chip.dma_transfers    = 0;
    chip.dma_aborts       = 0;
    chip.selects          = 0;
    chip.programs         = 0;
    chip.erases           = 0;
    chip.refusals         = 0;
    chip.largest_transfer = 0U;
    chip.log_len          = 0;
    memset(chip.count, 0, sizeof(chip.count));
    mutex_takes           = 0;
    mutex_gives           = 0;
    mutex_held            = 0;
    mutex_last_wait       = 0U;
    sleeps                = 0;
}

/*****************************************************************************************************/
/**
 * @brief Note something a real chip would have got wrong.
 *
 * @param[in] what  Says what it was, for the failure message.
 */
static void chip_fault(const char *what)
{
    if (chip.faults == 0)
    {
        chip.fault = what;
    }

    chip.faults++;
}

/*****************************************************************************************************/
/**
 * @brief Whether one die is still programming or erasing.
 *
 * @param[in] die  The die.
 * @return true while busy.
 */
static bool die_busy(int die)
{
    return chip.never_ready ? (chip.busy_until_us[die] != 0U)
                            : (now_us < chip.busy_until_us[die]);
}

/*****************************************************************************************************/
/**
 * @brief Whether any die of the chip is still programming or erasing.
 *
 * @return true while busy.
 */
static bool chip_busy(void)
{
    bool busy = false;
    int  die  = 0;

    for (die = 0; die < chip.dice; die++)
    {
        busy = busy || die_busy(die);
    }

    return busy;
}

/*****************************************************************************************************/
/**
 * @brief The die an address falls on.
 *
 * @param[in] address  The address.
 * @return 0 on a chip of one die.
 */
static int die_of(uint32_t address)
{
    return (chip.dice > 1) ? (int)(address / DIE_BYTES) : 0;
}

/*****************************************************************************************************/
/**
 * @brief Whether a command is one of the dedicated 4 byte ones.
 *
 * @param[in] opcode  The command.
 * @return true for 0Ch, 12h, 13h, 21h and DCh.
 */
static bool chip_is_4b(uint8_t opcode)
{
    return (opcode == 0x0CU) || (opcode == 0x12U) || (opcode == 0x13U) || (opcode == 0x21U) ||
           (opcode == 0xDCU);
}

/*****************************************************************************************************/
/**
 * @brief How many address bytes the chip takes after this command, as things stand.
 *
 * @param[in] opcode  The command.
 * @return 3 or 4.
 */
static uint32_t chip_address_bytes(uint8_t opcode)
{
    return (chip_is_4b(opcode) || chip.mode_4byte) ? 4U : 3U;
}

/*****************************************************************************************************/
/**
 * @brief The address the chip took in this transaction.
 *
 * @return The address, most significant byte first, as a chip reads it.
 */
static uint32_t chip_address(void)
{
    uint32_t count   = chip_address_bytes(chip.buf[0]);
    uint32_t address = 0U;
    uint32_t i       = 0U;

    for (i = 1U; (i <= count) && (i < chip.buf_len); i++)
    {
        address = (address << 8U) | chip.buf[i];
    }

    return address;
}

/*****************************************************************************************************/
/**
 * @brief Take one byte the MCU sent.
 *
 * @param[in] byte  The byte.
 */
static void chip_take(uint8_t byte)
{
    if (chip.clocked == 0U)
    {
        if (chip.log_len < (int)RECORD_MAX)
        {
            chip.log[chip.log_len] = byte;
        }

        chip.log_len++;
        chip.count[byte]++;

        if ((chip.errors != 0U) && (byte != 0x05U) && (byte != 0x30U))
        {
            /* A Spansion that refused something takes nothing else until told. */
            chip_fault("spoke to a chip waiting for its error to be cleared");
            chip.ignored = true;
        }
        else if (chip_busy() && (byte != 0x05U) && (byte != 0x70U) && (byte != 0xC2U))
        {
            /* A busy chip answers a status read and nothing else. */
            chip_fault("spoke to the chip while it was busy");
            chip.ignored = true;
        }
        else
        {
            /* Taken. */
        }
    }

    if (chip.buf_len < BUF_MAX)
    {
        chip.buf[chip.buf_len] = byte;
        chip.buf_len++;
    }
    else
    {
        chip_fault("sent more than a command and a page");
    }

    chip.clocked++;
}

/*****************************************************************************************************/
/**
 * @brief Give one byte to the MCU, as the command of this transaction says.
 *
 * @return The byte on MISO.
 */
static uint8_t chip_give(void)
{
    uint32_t position = chip.clocked;
    uint8_t  out      = 0xFFU;
    uint8_t  opcode   = chip.buf[0];
    uint32_t header   = 0U;

    chip.clocked++;

    if (!chip.present || chip.ignored || (chip.buf_len == 0U))
    {
        /* Nobody drives MISO, and the pull up reads as ones. */
    }
    else if (opcode == 0x9FU)
    {
        out = ((position >= 1U) && (position <= 6U)) ? chip.id[position - 1U] : 0xFFU;
    }
    else if (opcode == 0x05U)
    {
        out = chip_status();
    }
    else if ((opcode == 0x70U) && (chip.kind == KIND_MICRON))
    {
        out = (uint8_t)((chip_busy() ? 0x00U : 0x80U) | chip.flags);
    }
    else if ((opcode == 0x35U) && (chip.kind == KIND_SST26))
    {
        out = chip.locked_for_ever ? 0x00U : 0x08U;
    }
    else if ((opcode == 0x0BU) || (opcode == 0x0CU))
    {
        /* Fast read: the address, one dummy byte, then the data. */
        header = 1U + chip_address_bytes(opcode) + 1U;

        if ((position >= header) && (chip.buf_len >= header))
        {
            uint32_t at = (chip_address() + (position - header)) % chip.size;

            out = (at < CHIP_MAX_BYTES) ? chip_mem[at] : 0xFFU;
        }
        else
        {
            chip_fault("read before the address and dummy byte were sent");
        }
    }
    else
    {
        out = chip_id_byte(position);
    }

    return out;
}

/*****************************************************************************************************/
/**
 * @brief Status register 1, laid out as this kind of chip lays it out.
 *
 * @return The register.
 */
static uint8_t chip_status(void)
{
    bool    busy   = (chip.dice > 1) ? die_busy(chip.die) : chip_busy();
    uint8_t status = (uint8_t)((busy ? 0x01U : 0x00U) | (chip.wel ? 0x02U : 0x00U));

    if ((chip.kind == KIND_AT25DF) && (chip.id[0] == SPIF_MANUFACTURER_ATMEL))
    {
        /* Bits 3:2 the sectors protected, bit 4 the WP pin, bit 7 SPRL. */
        status |= (uint8_t)((chip.locked ? 0x0CU : 0x00U) | (chip.wp_low ? 0x00U : 0x10U) |
                            (chip.sprl ? 0x80U : 0x00U));
    }
    else if (chip.kind == KIND_AT25DF)
    {
        /* ESMT: locked shows as every BP bit set. */
        status |= chip.locked ? 0x1CU : 0x00U;
    }
    else if (chip.kind == KIND_SST26)
    {
        status |= busy ? 0x80U : 0x00U;
    }
    else
    {
        status |= (uint8_t)((chip.bp & 0x07U) << 2U);

        /* A Spansion with an error stays busy until told. */
        if (chip.errors != 0U)
        {
            status |= (uint8_t)(chip.errors | 0x01U);
        }
    }

    return status;
}

/*****************************************************************************************************/
/**
 * @brief Give a byte of the unique ID, read the way this kind of chip reads it.
 *
 * @param[in] position  Bytes clocked before this one.
 * @return The byte on MISO.
 */
static uint8_t chip_id_byte(uint32_t position)
{
    uint8_t  opcode  = chip.buf[0];
    uint32_t header  = 0U;
    uint32_t address = 0U;
    uint8_t  out     = 0xFFU;

    if ((opcode == 0x4BU) && (chip.kind == KIND_WINBOND))
    {
        /* Winbond: four dummy bytes, five in 4 byte address mode. */
        header = chip.mode_4byte ? 6U : 5U;
    }
    else if ((opcode == 0x4BU) && (chip.kind == KIND_SPANSION))
    {
        /* Spansion's OTP read: three address bytes and a dummy, always. */
        header = 5U;
    }
    else if ((opcode == 0x88U) && (chip.kind == KIND_SST26))
    {
        /* SST26's Security ID: two address bytes and a dummy. */
        header = 4U;
    }
    else if ((opcode == 0x77U) && (chip.kind == KIND_AT25DF) &&
             (chip.id[0] == SPIF_MANUFACTURER_ATMEL))
    {
        /* AT25DF's OTP register: three address bytes and two dummies. */
        header  = 6U;
        address = ((uint32_t)chip.buf[1] << 16U) | ((uint32_t)chip.buf[2] << 8U) | chip.buf[3];
    }
    else
    {
        chip_fault("read from a command this chip does not answer");
    }

    if ((header != 0U) && ((chip.buf_len != header) || (position < header) ||
                           (position >= (header + SPIF_UNIQUE_ID_SIZE))))
    {
        chip_fault("read the unique ID with the wrong number of address or dummy bytes");
    }
    else if (opcode == 0x77U)
    {
        out = chip.otp[(address + (position - header)) & 0x7FU];
    }
    else if ((header != 0U) && (chip.kind == KIND_SPANSION))
    {
        out = chip.otp[position - header];
    }
    else if (header != 0U)
    {
        out = chip.unique_id[position - header];
    }
    else
    {
        /* Faulted above. */
    }

    return out;
}

/*****************************************************************************************************/
/**
 * @brief Do what the transaction asked, now that chip select has gone high.
 */
static void chip_execute(void)
{
    uint8_t opcode = chip.buf[0];
    bool    big    = (chip.kind == KIND_M25P) || (chip.kind == KIND_SPANSION);

    if (chip.ignored || (chip.buf_len == 0U) || !chip.present)
    {
        return;
    }

    if (chip_is_4b(opcode) && !chip.has_4b_commands)
    {
        chip_fault("a 4 byte command this chip does not have");
        return;
    }

    switch (opcode)
    {
        case 0x06U:
            chip.wel = true;
            break;

        case 0x04U:
            chip.wel = false;
            break;

        case 0xABU:
        case 0x9FU:
        case 0x05U:
        case 0x0BU:
        case 0x0CU:
        case 0x4BU:
        case 0x70U:
        case 0x35U:
        case 0x88U:
        case 0x77U:
            /* Reads, and the wake up, which changes nothing the tests see. */
            break;

        case 0xB7U:
            if (!chip.has_4byte_mode)
            {
                chip_fault("switched a chip that has no 4 byte mode");
            }
            else if (!chip.mode_needs_wren || chip.wel)
            {
                chip.mode_4byte = true;
            }
            else
            {
                /* Micron ignores it without a write enable. */
            }
            break;

        case 0xC2U:
            if ((chip.dice < 2) || (chip.buf_len != 2U) || (chip.buf[1] >= (uint8_t)chip.dice))
            {
                chip_fault("selected a die that is not there");
            }
            else
            {
                chip.die = chip.buf[1];
            }
            break;

        case 0x01U:
            chip_write_status();
            break;

        case 0x30U:
            if (chip.kind != KIND_SPANSION)
            {
                chip_fault("cleared a Spansion error on another chip");
            }
            chip.errors = 0U;
            break;

        case 0x50U:
            if (chip.kind != KIND_MICRON)
            {
                chip_fault("cleared a Micron flag on another chip");
            }
            chip.flags = 0U;
            break;

        case 0x98U:
            if ((chip.kind != KIND_SST26) || !chip.wel)
            {
                chip_fault("an SST26 unlock without a write enable, or on another chip");
            }
            chip.locked = chip.locked_for_ever;
            chip.wel    = false;
            break;

        case 0x02U:
        case 0x12U:
            chip_program();
            break;

        case 0x20U:
        case 0x21U:
            if (big)
            {
                chip_fault("a 4 KB erase on a chip without one");
            }
            else
            {
                chip_erase((uint32_t)KB4, chip.sector_us);
            }
            break;

        case 0x52U:
            if (big || (chip.kind == KIND_SST26))
            {
                chip_fault("a 32 KB erase on a chip without one");
            }
            else
            {
                chip_erase((uint32_t)KB32, chip.block32_us);
            }
            break;

        case 0xD8U:
        case 0xDCU:
            if (chip.kind == KIND_SST26)
            {
                chip_fault("a block erase, which clears 8 KB or 32 KB near an SST26's ends");
            }
            else
            {
                chip_erase(big ? chip.big_sector : (uint32_t)KB64, chip.block_us);
            }
            break;

        case 0xC7U:
            if (chip.size > DIE_BYTES)
            {
                chip_fault("a chip erase on a stacked chip");
            }
            else
            {
                chip_erase(0U, chip.chip_us);
            }
            break;

        default:
            chip_fault("a command the model does not know");
            break;
    }
}

/*****************************************************************************************************/
/**
 * @brief Write the status register: only an AT25DF or an ESMT chip expects it, to unlock.
 */
static void chip_write_status(void)
{
    uint8_t data = (chip.buf_len >= 2U) ? chip.buf[1] : 0xFFU;

    if (chip.kind != KIND_AT25DF)
    {
        chip_fault("wrote the status register of a chip that does not need it");
    }
    else if (!chip.wel || (chip.buf_len != 2U))
    {
        chip_fault("a status write without a write enable, or with the wrong length");
    }
    else if (chip.id[0] != SPIF_MANUFACTURER_ATMEL)
    {
        chip.locked = ((data & 0x1CU) != 0U);
    }
    else if (chip.sprl)
    {
        /* With SPRL set, a write only clears it, and only with WP high. */
        chip.sprl = chip.wp_low;
    }
    else
    {
        /* 00h is a global unprotect, 3Ch in bits 5:2 a global protect. */
        chip.locked = ((data & 0x3CU) == 0x3CU);
        chip.sprl   = ((data & 0x80U) != 0U);
    }

    chip.wel              = false;
    chip.busy_until_us[0] = now_us + STATUS_US;
}

/*****************************************************************************************************/
/**
 * @brief Whether the chip refuses a program or erase, and what it shows for it.
 *
 * @param[in] program  A program rather than an erase.
 * @return true when refused.
 */
static bool chip_refuses(bool program)
{
    bool refused = ((chip.kind == KIND_SST26) || (chip.kind == KIND_AT25DF)) ? chip.locked
                                                                            : (chip.bp != 0U);

    if (refused)
    {
        chip.refusals++;

        if (chip.kind == KIND_MICRON)
        {
            /* Program or erase error, and the protection error; WEL stays. */
            chip.flags |= (uint8_t)((program ? 0x10U : 0x20U) | 0x02U);
        }
        else if (chip.kind == KIND_SPANSION)
        {
            /* Program or erase error, which keeps it busy; WEL stays. */
            chip.errors |= program ? 0x40U : 0x20U;
        }
        else
        {
            /* Ignored without a word. */
            chip.wel = false;
        }
    }

    return refused;
}

/*****************************************************************************************************/
/**
 * @brief Program what this transaction carried, wrapping inside its page as a real chip does.
 */
static void chip_program(void)
{
    uint32_t header  = 1U + chip_address_bytes(chip.buf[0]);
    uint32_t address = chip_address();
    uint32_t len     = (chip.buf_len > header) ? (chip.buf_len - header) : 0U;
    uint32_t base    = address & ~(SPIF_PAGE_SIZE - 1U);
    uint32_t i       = 0U;

    if (!chip.wel)
    {
        chip_fault("programmed without a write enable");
    }
    else if ((len == 0U) || (address >= chip.size))
    {
        chip_fault("programmed nothing, or past the end of the chip");
    }
    else if (!chip_refuses(true))
    {
        if (((address % SPIF_PAGE_SIZE) + len) > SPIF_PAGE_SIZE)
        {
            chip_fault("a write ran past the end of its page and wrapped");
        }

        /* Programming can only clear bits. Beyond what the model keeps it is
           not stored, but takes its time. */
        for (i = 0U; (i < len) && (address < CHIP_MAX_BYTES); i++)
        {
            chip_mem[base + (((address % SPIF_PAGE_SIZE) + i) % SPIF_PAGE_SIZE)] &=
                chip.buf[header + i];
        }

        if (chip.programs < (int)RECORD_MAX)
        {
            chip.program_sizes[chip.programs] = len;
        }

        chip.programs++;
        chip.wel                            = false;
        chip.busy_until_us[die_of(address)] = now_us + chip.page_us;
    }
    else
    {
        /* Refused. */
    }
}

/*****************************************************************************************************/
/**
 * @brief Erase the unit holding the address this transaction carried, or the whole chip.
 *
 * @param[in] unit     Bytes in a unit, or 0 for the whole chip.
 * @param[in] busy_us  How long the chip is busy afterwards.
 */
static void chip_erase(uint32_t unit, uint32_t busy_us)
{
    uint32_t start = 0U;
    uint32_t len   = chip.size;
    int      die   = 0;

    if (!chip.wel)
    {
        chip_fault("erased without a write enable");
        return;
    }

    if (unit != 0U)
    {
        if (chip.buf_len != (1U + chip_address_bytes(chip.buf[0])))
        {
            chip_fault("an erase with the wrong number of address bytes");
            return;
        }

        /* The chip erases the whole unit the address falls in. */
        start = chip_address() & ~(unit - 1U);
        len   = unit;
    }
    else if (chip.buf_len != 1U)
    {
        chip_fault("a chip erase with an address");
        return;
    }

    if (chip_refuses(false))
    {
        return;
    }

    if (start < CHIP_MAX_BYTES)
    {
        memset(&chip_mem[start], 0xFF,
               ((start + len) <= CHIP_MAX_BYTES) ? len : (CHIP_MAX_BYTES - start));
    }

    chip.erases++;
    chip.wel = false;

    if (unit == 0U)
    {
        for (die = 0; die < chip.dice; die++)
        {
            chip.busy_until_us[die] = now_us + busy_us;
        }
    }
    else
    {
        chip.busy_until_us[die_of(start)] = now_us + busy_us;
    }
}

/*****************************************************************************************************/
/**
 * @brief Start an SPI call: check it, count it, and say whether it goes ahead.
 *
 * @param[in]  hspi  The handle it was made on.
 * @param[in]  size  Its length.
 * @param[out] fail  What the call returns when it does not go ahead.
 * @return true when the bytes move.
 */
static bool chip_spi_begin(const SPI_HandleTypeDef *hspi, uint16_t size, HAL_StatusTypeDef *fail)
{
    bool go = true;

    chip.transfers++;

    if (hspi != &test_spi)
    {
        chip_fault("a transfer went to the wrong SPI handle");
    }

    if (chip.dma_running)
    {
        chip_fault("used the SPI while its DMA was still running");
    }

    if (size == 0U)
    {
        chip_fault("the HAL was handed no data, which it refuses");
    }

    if (!chip.cs_low)
    {
        chip_fault("talked to the chip without selecting it");
    }

    if (size > chip.largest_transfer)
    {
        chip.largest_transfer = size;
    }

    if ((mutex_created_at != NULL) && (mutex_held == 0))
    {
        chip_fault("a transfer ran without the mutex");
    }

    if (chip.transfers == chip.fail_at)
    {
        *fail = chip.fail_with;
        go    = false;
    }

    return go;
}

/*****************************************************************************************************/
/**
 * @brief Put a new Winbond style chip on the bus and hand it to spif_init().
 *
 * @param[in] manufacturer  JEDEC maker.
 * @param[in] capacity      JEDEC capacity code.
 * @return What spif_init() returned. The counts start from zero after.
 */
static spif_err_t init_chip(uint8_t manufacturer, uint8_t capacity)
{
    return init_kind(KIND_WINBOND, manufacturer, 0x40U, capacity, chip_size_of(capacity));
}

/*****************************************************************************************************/
/**
 * @brief Put a new chip of one kind on the bus and hand it to spif_init().
 *
 * @param[in] kind      Which maker's ways it follows.
 * @param[in] maker     JEDEC maker.
 * @param[in] type      JEDEC memory type.
 * @param[in] capacity  JEDEC capacity code.
 * @param[in] size      Bytes.
 * @return What spif_init() returned. The counts start from zero after.
 */
static spif_err_t init_kind(chip_kind_t kind, uint8_t maker, uint8_t type, uint8_t capacity,
                            uint32_t size)
{
    spif_err_t err = SPIF_ERR_NONE;

    chip_new(kind, maker, type, capacity, size);

    /* Spansion has no 4 byte mode. */
    chip.has_4byte_mode = (maker != SPIF_MANUFACTURER_SPANSION);

    /* A new handle, so the mutex a previous init made is not this one's. */
    mutex_created_at = NULL;
    mutex_creates    = 0;

    err = spif_init(&flash, &test_spi, &test_cs_port, CS_PIN);

    counts_clear();

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Finish a read, write or erase the way a user would: wait until it has ended.
 *
 * @param[in] err  What the call returned.
 * @return How it ended: the call's own error when it started nothing.
 */
static spif_err_t settle(spif_err_t err)
{
    if (err == SPIF_ERR_NONE)
    {
        err = spif_wait(&flash);
    }

    return err;
}

/*****************************************************************************************************/
/**
 * @brief Read, and wait until the read has finished.
 *
 * @return How it ended.
 */
static spif_err_t read_all(uint32_t address, uint8_t *data, size_t len)
{
    return settle(spif_read(&flash, address, data, len));
}

/*****************************************************************************************************/
/**
 * @brief Write, and wait until the last page is stored.
 *
 * @return How it ended.
 */
static spif_err_t write_all(uint32_t address, const uint8_t *data, size_t len)
{
    return settle(spif_write(&flash, address, data, len));
}

/*****************************************************************************************************/
/**
 * @brief Erase a sector, and wait until it is done.
 *
 * @return How it ended.
 */
static spif_err_t erase_sector(uint32_t sector)
{
    return settle(spif_erase_sector(&flash, sector));
}

/*****************************************************************************************************/
/**
 * @brief Erase a 32 KB block, and wait until it is done.
 *
 * @return How it ended.
 */
static spif_err_t erase_block32(uint32_t block32)
{
    return settle(spif_erase_block32(&flash, block32));
}

/*****************************************************************************************************/
/**
 * @brief Erase a 64 KB block, and wait until it is done.
 *
 * @return How it ended.
 */
static spif_err_t erase_block(uint32_t block)
{
    return settle(spif_erase_block(&flash, block));
}

/*****************************************************************************************************/
/**
 * @brief Erase the whole chip, and wait until it is done.
 *
 * @return How it ended.
 */
static spif_err_t erase_chip(void)
{
    return settle(spif_erase_chip(&flash));
}

/*****************************************************************************************************/
/**
 * @brief Fill pattern with bytes that do not repeat on any page or sector boundary.
 *
 * @param[in] seed  Changes the bytes from one call to the next.
 */
static void pattern_fill(uint32_t seed)
{
    uint32_t state = seed + 1U;
    uint32_t i     = 0U;

    for (i = 0U; i < PATTERN_BYTES; i++)
    {
        state      = (state * 1103515245U) + 12345U;
        pattern[i] = (uint8_t)(state >> 16U);
    }
}

/*****************************************************************************************************/
/**
 * @brief Make one call with a NULL pointer, and say whether assert_param stopped it.
 *
 * @param[in] which  0 to NULL_CALLS - 1: each public call that takes a pointer, one NULL.
 * @return true when the call stopped at an assert instead of returning.
 */
static bool assert_stops(int which)
{
    bool    stopped = true;
    uint8_t id[SPIF_UNIQUE_ID_SIZE];

    assert_expected = true;

    if (setjmp(assert_return) == 0)
    {
        switch (which)
        {
            case 0:
                (void)spif_init(NULL, &test_spi, &test_cs_port, CS_PIN);
                break;

            case 1:
                (void)spif_init(&flash, NULL, &test_cs_port, CS_PIN);
                break;

            case 2:
                (void)spif_init(&flash, &test_spi, NULL, CS_PIN);
                break;

            case 3:
                (void)spif_read(NULL, 0U, readback, 1U);
                break;

            case 4:
                (void)spif_read(&flash, 0U, NULL, 1U);
                break;

            case 5:
                (void)spif_write(NULL, 0U, pattern, 1U);
                break;

            case 6:
                (void)spif_write(&flash, 0U, NULL, 1U);
                break;

            case 7:
                (void)spif_erase_sector(NULL, 0U);
                break;

            case 8:
                (void)spif_erase_block32(NULL, 0U);
                break;

            case 9:
                (void)spif_erase_block(NULL, 0U);
                break;

            case 10:
                (void)spif_erase_chip(NULL);
                break;

            case 11:
                (void)spif_unique_id(NULL, id);
                break;

            case 12:
                (void)spif_unique_id(&flash, NULL);
                break;

            case 13:
                (void)spif_is_busy(NULL);
                break;

            default:
                (void)spif_wait(NULL);
                break;
        }

        /* Back here means no assert stopped it. */
        stopped = false;
    }

    assert_expected = false;

    return stopped;
}
