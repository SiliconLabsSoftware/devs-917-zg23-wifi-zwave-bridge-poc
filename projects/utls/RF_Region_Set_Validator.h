/*******************************************************************************
 * @file  RF_Region_Set_Validator.h
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

#ifndef RF_REGION_SET_VALIDATOR_
#define RF_REGION_SET_VALIDATOR_

/*Filtering valid RF region value from a whitelist when parasing zipgateway.cfg */
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <unistd.h>

/* valid RF region value used in ZW_RF_REGION_SET and ZW_RF_REGION_GET */
enum {
  ZW_EU = 0x00,  /* Europe */
  ZW_US = 0x01,  /* US */
  ZW_ANZ = 0x02, /* Australia & New Zealand */
  ZW_HK = 0x03,  /* Hong Kong */
  ZW_ID = 0x05,  /* India */
  ZW_IL = 0x06,  /* Israel */
  ZW_RU = 0x07,  /* Russia */
  ZW_CN = 0x08,  /* China */
  ZW_US_LR = 0x09,  /* US Long Range*/
  ZW_JP = 0x20,  /* Japan */
  ZW_KR = 0x21   /* Korea */
};

uint8_t RF_REGION_CHECK(uint8_t region_value);

#endif
