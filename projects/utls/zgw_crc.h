/*******************************************************************************
 * @file  zgw_crc.h
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

#ifndef ZGW_CRC_H_
#define ZGW_CRC_H_

#include <stdint.h>

/**
 * CRC-16 verification
 * @param crc initial crc value
 * @param[in] data raw data
 * @param data_len length of raw data
 * @return return unsigned crc-16 value
 */

#define CRC_POLY       0x1021
#define CRC_INIT_VALUE 0x1D0F

uint16_t zgw_crc16(uint16_t crc16, uint8_t *data, unsigned long data_len);

void calc_md5(uint8_t *addr, uint32_t len, uint8_t out_md5[16]) ;

#endif
