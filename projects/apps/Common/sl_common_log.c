/***************************************************************************/ /**
 * @file sl_common_log.c
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
#include <stdio.h>
#include <stdint.h>
#include "sl_common_log.h"

/**
 * @brief Print hexadecimal buffer content with spaces
 *
 * This function prints each byte of a buffer in hexadecimal format
 * with spaces between bytes, followed by a newline.
 *
 * @param[in] buf Pointer to the buffer to print
 * @param[in] len Length of the buffer in bytes
 */
void sl_print_hex_buf(const uint8_t *buf, uint32_t len)
{
  for (uint32_t i = 0; i < len; i++) {
    LOG_PRINTF("%02X ", buf[i]);
  }
  LOG_PRINTF("\n");
}

/**
 * @brief Print a 16-byte key or buffer in hexadecimal format
 *
 * This function prints a 16-byte buffer (typically a cryptographic key)
 * in hexadecimal format without spaces between bytes.
 *
 * @param[in] buf Pointer to the 16-byte buffer to print
 */
void sl_print_key(const uint8_t *buf)
{
  WRN_PRINTF(
    "%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X%02X\n",
    buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8],
    buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
}

/**
 * @brief Print hexadecimal buffer content without spaces
 *
 * This function prints each byte of a buffer in hexadecimal format
 * without spaces between bytes, followed by a newline.
 *
 * @param[in] buf Pointer to the buffer to print
 * @param[in] len Length of the buffer in bytes
 */
void
sl_print_hex_to_string(const uint8_t* buf, int len)
{
  int i;
  for (i = 0; i < len; i++) {
    LOG_PRINTF("%02X", buf[i]);
  }
  LOG_PRINTF("\n");
}
