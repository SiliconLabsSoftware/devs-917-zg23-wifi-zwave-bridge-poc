/***************************************************************************/ /**
 * @file sl_common_log.h
 * @brief log function
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/

#ifndef SL_ZW_LOG_H_
#define SL_ZW_LOG_H_

#include "FreeRTOS.h"

// original add this to setting ide.

extern osMutexId_t sl_log_mutex;

#define SL_LOG_MUTEX_ENABLED    1
#define SL_LOG_MUTEX_TIMEOUT_MS 1000

#if (SL_LOG_MUTEX_ENABLED)
#define SL_LOG_PRINT(...)                                  \
  {                                                        \
    osMutexAcquire(sl_log_mutex, SL_LOG_MUTEX_TIMEOUT_MS); \
    printf(__VA_ARGS__);                                   \
    osMutexRelease(sl_log_mutex);                          \
  }

#define DBG_PRINTF SL_LOG_PRINT
#define LOG_PRINTF SL_LOG_PRINT
#define WRN_PRINTF SL_LOG_PRINT
#define ERR_PRINTF SL_LOG_PRINT

#else
#define SL_LOG_PRINT(...) printf(__VA_ARGS__)

#define DBG_PRINTF SL_LOG_PRINT
#define LOG_PRINTF SL_LOG_PRINT
#define WRN_PRINTF SL_LOG_PRINT
#define ERR_PRINTF SL_LOG_PRINT
#endif

#ifdef SERIALAPI_DEBUG
#define ASSERT(a)      \
  {                    \
    if (!(a)) {        \
      __asm("int $3"); \
    }                  \
  }
#else
#define ASSERT(a)                                                    \
  {                                                                  \
    if (!(a)) {                                                      \
      LOG_PRINTF("Assertion failed at %s:%i\n", __FILE__, __LINE__); \
    }                                                                \
  }
#endif

/* Macro to call a function pointer if it's not NULL */
#define SL_CALL_IF_NOT_NULL(func_ptr, ...) \
  do {                                     \
    if ((func_ptr) != NULL) {              \
      (func_ptr)(__VA_ARGS__);             \
    }                                      \
  } while (0)

/**
 * @brief Print hexadecimal buffer content with spaces
 *
 * This function prints each byte of a buffer in hexadecimal format
 * with spaces between bytes, followed by a newline.
 *
 * @param[in] buf Pointer to the buffer to print
 * @param[in] len Length of the buffer in bytes
 */
void sl_print_hex_buf(const uint8_t *buf, uint32_t len);

/**
 * @brief Print hexadecimal buffer content without spaces
 *
 * This function prints each byte of a buffer in hexadecimal format
 * without spaces between bytes, followed by a newline.
 *
 * @param[in] buf Pointer to the buffer to print
 * @param[in] len Length of the buffer in bytes
 */
void sl_print_hex_to_string(const uint8_t *buf, int len);

/**
 * @brief Print a 16-byte key or buffer in hexadecimal format
 *
 * This function prints a 16-byte buffer (typically a cryptographic key)
 * in hexadecimal format without spaces between bytes.
 *
 * @param[in] buf Pointer to the 16-byte buffer to print
 */
void sl_print_key(const uint8_t *buf);

#endif /* SL_ZW_LOG_H_ */
