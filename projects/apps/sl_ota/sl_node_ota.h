/*******************************************************************************
 * @file  sl_node_ota.h
 * @brief OTA function for the Node
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

/**
 * @brief Send a Firmware Update Meta Data Request Get (0x7A 0x03) command to the OTA node.
 *
 * @param checksum The firmware checksum (CRC).
 * @return 0 on success, -1 on error.
 */
int sl_node_ota_send_md_request(uint16_t checksum);

/**
 * @brief Set the firmware size and node ID for the OTA process.
 *
 * @param s Firmware size in bytes.
 * @param nodeid Node ID of the OTA target.
 */
void sl_node_ota_set_fsize(uint32_t s, uint8_t nodeid);

/**
 * @brief Send multiple firmware chunk packets to the OTA node.
 *
 * @param ch_id Starting chunk sequence number (starts from 1).
 * @param ch_num Number of chunks to send.
 * @return 0 on success, -1 on error.
 */
int sl_node_ota_send_md_chunks(uint16_t ch_id, uint16_t ch_num);
