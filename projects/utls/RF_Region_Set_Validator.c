/*******************************************************************************
 * @file  RF_Region_Set_Validator.c
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

#include "RF_Region_Set_Validator.h"
#include "Common/sl_common_log.h"

uint8_t RF_REGION_CHECK(uint8_t region_value)
{
  uint8_t RFregion;
  /*Filtering the RF Region with valid values*/
  switch (region_value) {
    case ZW_EU:
      RFregion = region_value;
      DBG_PRINTF("RF Region EU = %d \n", region_value);
      break;
    case ZW_US:
      RFregion = region_value;
      DBG_PRINTF("RF Region US = %d \n", region_value);
      break;
    case ZW_ANZ:
      RFregion = region_value;
      DBG_PRINTF("RF Region Australia/ New Zealand = %d \n", region_value);
      break;
    case ZW_HK:
      RFregion = region_value;
      DBG_PRINTF("RF Region Hong Kong = %d \n", region_value);
      break;
    case ZW_ID:
      RFregion = region_value;
      DBG_PRINTF("RF Region India = %d \n", region_value);
      break;
    case ZW_IL:
      RFregion = region_value;
      DBG_PRINTF("RF Region Israel = %d \n", region_value);
      break;
    case ZW_RU:
      RFregion = region_value;
      DBG_PRINTF("RF Region Russia = %d \n", region_value);
      break;
    case ZW_CN:
      RFregion = region_value;
      DBG_PRINTF("RF Region China = %d \n", region_value);
      break;
    case ZW_US_LR:
      RFregion = region_value;
      DBG_PRINTF("RF Region US_LR = %d \n", region_value);
      break;
    case ZW_JP:
      RFregion = region_value;
      DBG_PRINTF("RF Region Japan = %d \n", region_value);
      break;
    case ZW_KR:
      RFregion = region_value;
      DBG_PRINTF("RF Region Korea = %d \n", region_value);
      break;
    default:
      RFregion = 0xFE;
      DBG_PRINTF("The RF Region value is not valid: %d \n", region_value);
  }
  return RFregion;
}
