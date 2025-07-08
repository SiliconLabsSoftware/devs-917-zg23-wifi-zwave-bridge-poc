/*******************************************************************************
 * @file  sl_si917_net.c
 * @brief aes functions
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

#include "sl_board_configuration.h"
#include "cmsis_os2.h"
#include "sl_wifi.h"
#include "sl_net.h"

#include "sl_utility.h"
#include "errno.h"
#include <string.h>
#include "sl_si91x_driver.h"
#include "sl_net_wifi_types.h"
#ifdef SLI_SI91X_MCU_INTERFACE
#include "rsi_rom_clks.h"
#endif
#include "sl_net_default_values.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net_si91x.h"
#include "sl_status.h"
#include "sl_common_log.h"

#include "sl_si917_net.h"

#ifndef TX_POOL_RATIO
#define TX_POOL_RATIO 1
#endif

#ifndef RX_POOL_RATIO
#define RX_POOL_RATIO 1
#endif

#ifndef GLOBAL_POOL_RATIO
#define GLOBAL_POOL_RATIO 1
#endif

/**
 * @brief Initialize the SI917 network interface and gateway structure.
 *
 * Fetches the Wi-Fi firmware version and MAC address, prints them,
 * and stores them in the provided gateway structure.
 *
 * @param gw Pointer to the gateway structure to initialize.
 * @return SL_STATUS_OK on success, error code otherwise.
 */
sl_status_t sl_si917_net_init(sl_si917_gw_t *gw)
{
  sl_status_t status = SL_STATUS_OK;

  status = sl_wifi_get_firmware_version(&gw->firmware_version);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\r\nFailed to fetch firmware version: 0x%lx\r\n", status);
    return status;
  } else {
    print_firmware_version(&gw->firmware_version);
  }

  status = sl_wifi_get_mac_address(SL_WIFI_CLIENT_INTERFACE, &gw->mac_addr);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\r\nFailed to get mac address: 0x%lx\r\n", status);
    return status;
  }
  LOG_PRINTF("\r\nDevice MAC address: %x:%x:%x:%x:%x:%x\r\n",
             gw->mac_addr.octet[0],
             gw->mac_addr.octet[1],
             gw->mac_addr.octet[2],
             gw->mac_addr.octet[3],
             gw->mac_addr.octet[4],
             gw->mac_addr.octet[5]);

  return status;
}

/**
 * @brief Connect to the Wi-Fi access point using the default profile.
 *
 * Retrieves the default Wi-Fi client profile and stores it in the gateway structure.
 * Also fetches and updates the default gateway and subnet information.
 *
 * @param gw Pointer to the gateway structure to update.
 * @return SL_STATUS_OK on success, error code otherwise.
 */
sl_status_t sl_s917_net_connect_ap(sl_si917_gw_t *gw)
{
  sl_status_t status = SL_STATUS_OK;

  status = sl_net_get_profile(SL_NET_WIFI_CLIENT_INTERFACE,
                              SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID,
                              &gw->profile);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to get client gw->profile: 0x%lx\r\n", status);
    return status;
  }
  LOG_PRINTF("\r\nSuccess to get client gw->profile\r\n");

  return status;
}
