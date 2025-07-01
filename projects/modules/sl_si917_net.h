/*******************************************************************************
 * @file  sl_si917_net.h
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

#ifndef SL_SI917_NET_H
#define SL_SI917_NET_H

#include "sl_net_wifi_types.h"

typedef struct {
  sl_wifi_firmware_version_t firmware_version;

  sl_net_wifi_client_profile_t profile;
  sl_ip_address_t ip_address;
  sl_mac_address_t mac_addr;
  uint8_t subnet[4];
  uint8_t default_gw[4];
} sl_si917_gw_t;

/**
 * @brief Initialize the SI917 network interface and gateway structure.
 *
 * Fetches the Wi-Fi firmware version and MAC address, prints them,
 * and stores them in the provided gateway structure.
 *
 * @param gw Pointer to the gateway structure to initialize.
 * @return SL_STATUS_OK on success, error code otherwise.
 */
sl_status_t sl_si917_net_init(sl_si917_gw_t *gw);

/**
 * @brief Connect to the Wi-Fi access point using the default profile.
 *
 * Retrieves the default Wi-Fi client profile and stores it in the gateway structure.
 * Also fetches and updates the default gateway and subnet information.
 *
 * @param gw Pointer to the gateway structure to update.
 * @return SL_STATUS_OK on success, error code otherwise.
 */
sl_status_t sl_s917_net_connect_ap(sl_si917_gw_t *gw);

/**
 * @brief Retrieve and update the default gateway and subnet mask.
 *
 * Sends a command to the SI91x driver to query network parameters.
 * Updates the gateway structure with the subnet mask and default gateway.
 * Prints the subnet and gateway addresses.
 *
 * @param gw Pointer to the gateway structure to update.
 */
void sl_si917_get_default_gw(sl_si917_gw_t *gw);

#endif // SL_SI917_NET_H
