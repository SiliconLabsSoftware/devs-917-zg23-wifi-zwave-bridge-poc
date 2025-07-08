/***************************************************************************/ /**
 * @file sl_infra_if.h
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

#ifndef SL_INFRA_IF_H
#define SL_INFRA_IF_H

#define SL_INFRA_IF_PIO_PREFIX "fd00:bbbb::"     ///< IPv6 prefix for the infrastructure interface
#define SL_INFRA_IF_PIO_PREFIX_LEN 64           ///< Length of the IPv6 prefix for the infrastructure interface
#define SL_INFRA_IF_RIO_PREFIX "fd00:bbbb:1::"//"fd00:aaaa:1::"//"fd00:bbbb::"   ///< IPv6 prefix for the infrastructure interface
#define SL_INFRA_IF_RIO_PREFIX_LEN 64           ///< Length of the IPv6 prefix for the infrastructure interface
#define SL_INFRA_IF_RA_PERIOD_MS 60000
#define SL_INFRA_IF_MULT_PREFIX "ff02::1"
/**
 * @brief Initializes the infrastructure interface.
 */
void sl_infra_if_init(void);

/**
 * @brief Deinitializes the infrastructure interface.
 */
void sl_infra_if_deinit(void);

#endif // SL_ZW_NETIF_H
