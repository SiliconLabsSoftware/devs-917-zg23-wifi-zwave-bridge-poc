/*******************************************************************************
 * @file  sl_ota.h
 * @brief OTA function
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
#ifndef SL_OTA_H
#define SL_OTA_H

/**
 * @brief Handle an OTA bridge data chunk.
 * @param chunk_data Pointer to the chunk data buffer.
 * @param chunk_len Length of the chunk data.
 * @return Status code (implementation-defined).
 */
int sl_ota_bridge_handler(uint8_t *chunk_data, uint16_t chunk_len);

/**
 * @brief Handle an OTA bridge ACK.
 * @param buf_ack Pointer to the ACK buffer.
 * @param len Pointer to variable to store the length of the ACK.
 * @return true if ACK is valid, false otherwise.
 */
bool sl_ota_bridge_ack(uint8_t *buf_ack, uint32_t *len);

/**
 * @brief Download firmware to the controller.
 * @param data Pointer to firmware data.
 * @param len Length of the firmware data.
 * @return Status code.
 */
sl_status_t sl_ota_download_controller_fw(void *data, uint16_t len);

/**
 * @brief Start the OTA process on the controller.
 * @return Status code.
 */
sl_status_t sl_ota_controller_start(void);

/**
 * @brief Download firmware to a node.
 * @param data Pointer to firmware data.
 * @param len Length of the firmware data.
 * @return Status code.
 */
sl_status_t sl_ota_download_node_fw(
  void *data,
  uint16_t len);

#endif /* SL_OTA_H */
