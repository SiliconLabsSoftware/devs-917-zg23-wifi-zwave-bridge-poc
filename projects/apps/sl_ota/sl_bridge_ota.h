/*******************************************************************************
 * @file  sl_bridge_ota.h
 * @brief Header file for bridge OTA functions
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
/*************************************************************************
 *
 */

#ifndef APPS_SL_BRIDGE_OTA_H_
#define APPS_SL_BRIDGE_OTA_H_

#include "sl_status.h"

/**
 * @brief Start the OTA process for the bridge device.
 * @param[in] char* The IP address of the local HTTP server.
 * @return sl_status_t See https://docs.silabs.com/gecko-platform/latest/platform-common/status for details.
 */

sl_status_t sl_bridge_ota(char* http_server_ip);

#endif /* APPS_SL_BRIDGE_OTA_H_ */
