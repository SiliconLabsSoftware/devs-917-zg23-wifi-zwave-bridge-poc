/*******************************************************************************
 * @file  sl_bridge_ota.c
 * @brief OTA function for the bridge
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

#include "sl_common_type.h"

#include <modules/sl_http.h>
#include "sl_bridge_ota.h"
#include "sl_status.h"
#include "sl_common_log.h"
#include "firmware_upgradation.h"
#include "sl_si91x_hal_soc_soft_reset.h"
#include "sl_wifi_callback_framework.h"

/****************************************************************************/
/*                            PUBLIC FUNCTIONS                              */
/****************************************************************************/

sl_status_t sl_bridge_ota(char* http_server_ip)
{
  (void) http_server_ip;
  return SL_STATUS_OK;
}
