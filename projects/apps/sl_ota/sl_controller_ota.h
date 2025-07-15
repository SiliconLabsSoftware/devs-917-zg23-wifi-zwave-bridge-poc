/***************************************************************************/ /**
 * @file sl_controller_ota.h
 * @brief Header file for OTA functions
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

#ifndef SL_CONTROLLER_OTA_H_
#define SL_CONTROLLER_OTA_H_

#include "sl_si91x_psram_handle.h"

#define PSRAM_CONTROLLER_IMG_ADDRESS PSRAM_BASE_ADDRESS

/**
 * @brief Start OTA for controller process.
 *
 * @return Status code indicating the result.
 *   - 1: Success
 *   - 0: Failure
 */
sl_status_t sl_start_controller_ota();

/**
 * @brief Store OTA controller image size.
 */
void sl_store_controller_ota_img_size(uint32_t controller_img_size);

void sl_ota_init();

#endif /* SL_CONTROLLER_OTA_H_ */
