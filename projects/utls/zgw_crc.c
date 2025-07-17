/*******************************************************************************
 * @file  zgw_crc.c
 * @brief
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

#include "zgw_crc.h"
#include "mbedtls/md5.h"

void calc_md5(uint8_t *addr, uint32_t len, uint8_t out_md5[16])
{
  mbedtls_md5_context ctx;
  mbedtls_md5_init(&ctx);
  mbedtls_md5_starts(&ctx);
  mbedtls_md5_update(&ctx, addr, len);
  mbedtls_md5_finish(&ctx, out_md5);
  mbedtls_md5_free(&ctx);
}
/*
 * CRC-16 verification
 */
uint16_t zgw_crc16(uint16_t crc16, uint8_t *data, unsigned long data_len)
{
  uint8_t crc_data;
  uint8_t bitmask;
  uint8_t new_bit;
  //printf("zgw_crc16: data_len = %u\r\n", data_len);
  while (data_len--) {
    crc_data = *data++;
    for (bitmask = 0x80; bitmask != 0; bitmask >>= 1) {
      /* Align test bit with next bit of the message byte, starting with msb. */
      new_bit = ((crc_data & bitmask) != 0) ^ ((crc16 & 0x8000) != 0);
      crc16 <<= 1;
      if (new_bit) {
        crc16 ^= CRC_POLY;
      }
    } /* for (bitMask = 0x80; bitMask != 0; bitMask >>= 1) */
  }
  return crc16;
}

uint16_t chksum(uint16_t sum, const uint8_t *data, uint16_t len)
{
  uint16_t t;
  const uint8_t *dataptr;
  const uint8_t *last_byte;

  dataptr = data;
  last_byte = data + len - 1;

  while (dataptr < last_byte) {   /* At least two more bytes */
    t = (dataptr[0] << 8) + dataptr[1];
    sum += t;
    if (sum < t) {
      sum++;      /* carry */
    }
    dataptr += 2;
  }

  if (dataptr == last_byte) {
    t = (dataptr[0] << 8) + 0;
    sum += t;
    if (sum < t) {
      sum++;      /* carry */
    }
  }

  /* Return sum in host byte order. */
  return sum;
}
