/***************************************************************************/ /**
 * @file sl_zw_router.h
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

#ifndef SL_ZW_ROUTER_H
#define SL_ZW_ROUTER_H

#include "Net/ZW_udp_server.h"
#include "Common/sl_rd_types.h"

/**
 * State flags of the \ref ZIP_Router components.
 *
 */
typedef enum zgw_state {
  /** ZGW process just launching */
  ZGW_BOOTING =                            0x0001,
  /** Bridge is busy. */
  ZGW_BRIDGE =                             0x0002,
  /** Network Management */
  ZGW_NM =                                 0x0004,
  /** Backup is requested or running. */
  ZGW_BU =                                 0x0400,
  /** ZIP Router is resetting. */
  ZGW_ROUTER_RESET =                       0x0800,
  /** Zipgateway is shutting down. */
  ZGW_SHUTTING_DOWN =                      0x8000,
} zgw_state_t;

/**
 * @brief Handles incoming raw Z-Wave frames from the serial interface.
 *
 * @param rxStatus   Receive status flags.
 * @param destNode   Destination node ID.
 * @param sourceNode Source node ID.
 * @param pCmd       Pointer to received command buffer.
 * @param cmdLength  Length of the command buffer.
 */
void sl_application_serial_handler(uint8_t rxStatus,
                                   nodeid_t destNode,
                                   nodeid_t sourceNode,
                                   ZW_APPLICATION_TX_BUFFER *pCmd,
                                   uint8_t cmdLength);

/**
 * @brief Handles incoming Z-Wave frames received over IP.
 *
 * @param c        Pointer to the Z-Wave connection structure.
 * @param pData    Pointer to received data buffer.
 * @param bDatalen Length of the received data.
 * @return true if the frame was handled, false otherwise.
 */
bool sl_application_cmd_ip_handler(zwave_connection_t *c, void *pData, u16_t bDatalen);

/**
 * @brief Handles incoming Z-Wave frames for ZIP (Z-Wave over IP).
 *
 * @param p        Pointer to transmission parameters.
 * @param pCmd     Pointer to received command buffer.
 * @param cmdLength Length of the command buffer.
 */
void sl_application_cmd_zip_handler(ts_param_t *p,
                                    ZW_APPLICATION_TX_BUFFER *pCmd,
                                    uint16_t cmdLength);

/**
 * @brief Called when the SerialAPI has started.
 *
 * @param pData  Pointer to initialization data.
 * @param length Length of the initialization data.
 */
void sl_serial_api_started(uint8_t *pData, uint8_t length);

/**
 * @brief Handles controller update events such as node inclusion/exclusion.
 *
 * @param bStatus         Status event code.
 * @param bNodeID         Node ID of the node that sent node info.
 * @param pCmd            Pointer to Application Node information.
 * @param bLen            Node info length.
 * @param prospectHomeID  NULL or the prospect homeid if smart start inclusion.
 */
void sl_appl_controller_update(BYTE bStatus,
                               nodeid_t bNodeID,
                               BYTE* pCmd,
                               BYTE bLen,
                               BYTE *prospectHomeID);

/**
 * @brief Initializes the Z-Wave router module.
 */
void sl_router_init(void);

/**
 * @brief Main processing function for the Z-Wave router module.
 */
void sl_router_process(void);

#endif // SL_ZW_ROUTER_H
