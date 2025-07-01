/***************************************************************************/ /**
 * @file zw_network_info.h
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

#ifndef ZW_NETWORK_INFO_H_
#define ZW_NETWORK_INFO_H_

#include <stdint.h>
#include "sl_rd_types.h"

/**
 * @{
 * \ingroup ZIP_Router
 */

/** Node ID of this zipgateway. */
extern nodeid_t MyNodeID;

/** Home ID of this zipgateway.
 *
 * In network byte order (big endian).
 */
extern uint32_t homeID;

/** @} */
#endif /* ZW_NETWORK_INFO_H_ */
