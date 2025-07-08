/*******************************************************************************
 * @file  sl_gw_info.h
 * @brief Store global information header file
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

#ifndef APPS_COMMON_SL_GW_INFO_H_
#define APPS_COMMON_SL_GW_INFO_H_

#include "sl_rd_types.h"
#include "sl_common_config.h"
#include "modules/sl_si917_net.h"

/**
 * @brief Current Z-Wave node ID for this device
 *
 * Global variable holding the Z-Wave node ID for this device in the network.
 */
extern nodeid_t MyNodeID;

/**
 * @brief Z-Wave network home ID
 *
 * Global variable containing the 32-bit home ID of the Z-Wave network.
 */
extern uint32_t homeID;

/**
 * @brief Z-Wave gateway router configuration
 *
 * Global configuration structure containing all router parameters including
 * network addresses, security settings, and operational parameters.
 */
extern sl_router_config_t router_cfg;

/**
 * @brief Gateway information structure
 *
 * Structure containing Silicon Labs gateway specific information.
 */
extern sl_si917_gw_t sl_gw_info;

/**
 * @brief Controller role definitions
 *
 * Enumeration defining the possible roles of a Z-Wave controller in the network.
 */
typedef enum {
  CTRL_SECONDARY,  /**< Secondary controller */
  CTRL_SUC,        /**< Static Update Controller */
  CTRL_SLAVE       /**< Slave controller */
} controller_role_t;

/**
 * @brief Current controller role
 *
 * Global variable indicating the current role of this controller in the Z-Wave network.
 */
extern controller_role_t controller_role;

/**
 * @brief Initialize the Z-Wave gateway configuration with default values
 *
 * This function initializes the router_cfg structure with default values
 * including network addresses, security settings, and device parameters.
 * It sets up IPv6 addresses for the gateway, LAN, PAN, and tunneling interfaces.
 */
void sl_config_init(void);

/**
 * @brief Convert an IPv6 address string to binary representation
 *
 * This function parses an IPv6 address string and converts it to its binary
 * representation for use with the networking stack.
 *
 * @param[in] addrstr The IPv6 address string to convert
 * @param[out] ipaddr Pointer to the output binary IPv6 address structure
 * @return 0 on success, non-zero on failure
 */
int sl_uiplib_ipaddrconv(const char *addrstr, uip_ipaddr_t *ipaddr);

#endif /* APPS_COMMON_SL_GW_INFO_H_ */
