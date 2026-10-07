/**
 * @file        osal.h
 * @brief       Host fake standing in for nimaltd/osal, so the tests decide what it answers.
 * @version     3.0.0
 *
 * @author      Nima Askari (NimaLTD)
 * @email       nima.askari@gmail.com
 * @github      https://www.github.com/nimaltd
 *
 * @copyright   (c) 2026 Nima Askari (NimaLTD)
 *              SPDX-License-Identifier: Apache-2.0
 *              See LICENSE.md in the project root for the full license text.
 *
 * @note        This file exists only so the library can be tested on a PC. It
 *              is not part of the shipped library: a real build uses osal.h
 *              from nimaltd/osal. The names and error values match it, and the
 *              four functions are implemented by the tests, which keep count
 *              of every lock and sleep and can make any of them fail.
 */

#ifndef OSAL_H
#define OSAL_H

/*
 * ****************************************************************************************************
 * Includes
 * ****************************************************************************************************
*/

#include <stdint.h>

/*
 * ****************************************************************************************************
 * Types
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Error values returned by the mutex functions, the same as osal's.
 */
typedef enum
{
    OSAL_ERR_NONE    = 0, /**< Done.                                              */
    OSAL_ERR_INVALID = 1, /**< A NULL mutex.                                      */
    OSAL_ERR_TIMEOUT = 2, /**< Another thread kept the mutex for the whole wait.  */
    OSAL_ERR_MUTEX   = 3, /**< The RTOS could not create or take the mutex.       */

} osal_err_t;

/*****************************************************************************************************/
/**
 * @brief A mutex. The tests tell one from another by its address.
 */
typedef struct
{
    uint8_t unused; /**< C wants a member. */

} osal_mutex_t;

/*
 * ****************************************************************************************************
 * Public function prototypes
 * ****************************************************************************************************
*/

/*****************************************************************************************************/
/**
 * @brief Create a mutex, unless the test says it cannot be made.
 */
osal_err_t osal_mutex_create(osal_mutex_t *mutex);

/*****************************************************************************************************/
/**
 * @brief Take a mutex, answering as the test chose.
 */
osal_err_t osal_mutex_lock(osal_mutex_t *mutex, uint32_t timeout_ms);

/*****************************************************************************************************/
/**
 * @brief Give a mutex back.
 */
void osal_mutex_unlock(osal_mutex_t *mutex);

/*****************************************************************************************************/
/**
 * @brief Sleep, by moving the tests' clock on.
 */
void osal_delay_ms(uint32_t ms);

#endif /* OSAL_H */
