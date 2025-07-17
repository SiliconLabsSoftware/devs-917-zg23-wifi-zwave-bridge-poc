/*******************************************************************************
 * @file  sl_ota.c
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
#include "cmsis_os2.h"
#include "sl_wifi.h"
#include "sl_net.h"
#include "sl_utility.h"
#include "errno.h"
#include <string.h>
#include "sl_si91x_driver.h"

#include "Common/sl_common_log.h"
#include "modules/sl_psram.h"
#include "utls/zgw_crc.h"

#include "sl_node_ota.h"
#include "sl_controller_ota.h"

#include "sl_ota.h"

#ifdef SLI_SI91X_MCU_INTERFACE
#include "sl_si91x_hal_soc_soft_reset.h"
#endif

#define FW_HEADER_SIZE 64
#define CHUNK_SIZE     1024

typedef struct {
  uint8_t type;
  uint16_t data_len;
  uint8_t data[1];
} __attribute__((packed)) sl_fw_chunk_t;

typedef struct {
  uint8_t md5[16];
  uint32_t fw_size;
  uint32_t chk_tot;
  uint32_t chk_id;
} sl_fw_info_t;

sl_fw_info_t sl_bridge_fw_info = {
  .chk_id  = 0,
  .chk_tot = 1,
  .fw_size = 0,
};

sl_fw_info_t sl_ctrl_fw_info = {
  .chk_id  = 0,
  .chk_tot = 1,
  .fw_size = 0,
};

sl_fw_info_t sl_node_fw_info = {
  .chk_id  = 0,
  .chk_tot = 1,
  .fw_size = 0,
};

int sl_node_ota_setup(void);

/*
 * note: packet = type (1byte) len (2byte) data (len bytes)
 *        type = SL_FWUP_RPS_HEADER:  header.
 *        else: firmware data.
 */
/**
 * @brief Handle an OTA bridge data chunk.
 * @param chunk_data Pointer to the chunk data buffer.
 * @param chunk_len Length of the chunk data.
 * @return Status code (implementation-defined).
 */
int sl_ota_bridge_handler(uint8_t *chunk_data, uint16_t chunk_len)
{
  sl_status_t status = SL_STATUS_OK;
  if (chunk_data == NULL || chunk_len == 0) {
    ERR_PRINTF("Invalid chunk data or length.\n");
    return SL_STATUS_FAIL;
  }
  sl_fw_chunk_t *chkpkt = (sl_fw_chunk_t *) chunk_data;

  LOG_PRINTF("Received chunk: %d\n", chkpkt->type);
  LOG_PRINTF("Received chunk len: %d\n", chunk_len);
  LOG_PRINTF("Chunk data len: %d\n", chkpkt->data_len);

  if (chkpkt->data_len > 0) {
    if (chkpkt->type == SL_FWUP_RPS_HEADER) {
      status = sl_wifi_get_firmware_size((void *) chkpkt->data,
                                         &sl_bridge_fw_info.fw_size);
      if (status != SL_STATUS_OK) {
        ERR_PRINTF("Unable to fetch firmware size. Status: 0x%lx\n", status);
        return SL_STATUS_FAIL;
      }
      LOG_PRINTF("\r\n Image size = 0x%lx\r\n", sl_bridge_fw_info.fw_size);
      sl_bridge_fw_info.chk_tot =
        ((sl_bridge_fw_info.fw_size - FW_HEADER_SIZE) / CHUNK_SIZE) + 1
        + 1 /*header*/;
      sl_bridge_fw_info.chk_id = 1;
      // Send RPS header which is received as first chunk
      status = sl_si91x_fwup_start(chkpkt->data);
    } else {
      // Send RPS content
      status = sl_si91x_fwup_load(chkpkt->data, chkpkt->data_len);
    }
  }
  if (status != SL_STATUS_OK) {
    if (status == SL_STATUS_SI91X_FW_UPDATE_DONE) {
      LOG_PRINTF("\r\nM4 Firmware update complete\r\n");

      sl_si91x_soc_nvic_reset();

      return SL_STATUS_OK;
    } else {
      ERR_PRINTF("\r\nFirmware update failed : 0x%lx\n", status);
      return SL_STATUS_FAIL;
    }
  }
  sl_bridge_fw_info.chk_id++;
  return status;
}

/**
 * @brief Handle an OTA bridge ACK.
 * @param buf_ack Pointer to the ACK buffer.
 * @param len Pointer to variable to store the length of the ACK.
 * @return true if ACK is valid, false otherwise.
 */
bool sl_ota_bridge_ack(uint8_t *buf_ack, uint32_t *len)
{
  uint16_t pktszie = CHUNK_SIZE;
  buf_ack[0]       = SL_FWUP_RPS_CONTENT;
  buf_ack[1]       = (pktszie & 0xFF);
  buf_ack[2]       = (pktszie >> 8);

  *len = 3;
  return true;
}

/**
 * @brief Download firmware to the controller.
 * @param data Pointer to firmware data.
 * @param len Length of the firmware data.
 * @return Status code.
 */
sl_status_t sl_ota_download_controller_fw(void *data, uint16_t len)
{
  if ((len == 0) && !data) {
    ERR_PRINTF("Invalid data or length for controller firmware download.\n");
    return SL_STATUS_FAIL;
  }
  sl_fw_chunk_t *chkpkt = (sl_fw_chunk_t *) data;

  LOG_PRINTF("Received chunk: %d\n", chkpkt->type);
  LOG_PRINTF("Received chunk len: %d\n", len);
  LOG_PRINTF("Chunk data len: %d\n", chkpkt->data_len);

  // Write received data to PSRAM
  if (chkpkt->data_len > 0) {
    if (chkpkt->type == SL_FWUP_RPS_HEADER) {
      sl_ctrl_fw_info.fw_size = chkpkt->data[0] | (chkpkt->data[1] << 8)
                                | (chkpkt->data[2] << 16)
                                | (chkpkt->data[3] << 24);
      memcpy(&sl_ctrl_fw_info.md5[0], &chkpkt->data[4], 16);
      sl_ctrl_fw_info.chk_tot =
        ((sl_ctrl_fw_info.fw_size) / CHUNK_SIZE) /*header*/;
      sl_ctrl_fw_info.chk_id = 0;
      LOG_PRINTF("\r\n Image size = %ld, md5: ", sl_ctrl_fw_info.fw_size);
      sl_print_hex_to_string(sl_ctrl_fw_info.md5, 16);
      LOG_PRINTF("\n");
    } else {
      uint32_t addr = sl_ctrl_fw_info.chk_id * CHUNK_SIZE;
      sl_psram_write_auto_mode((PSRAM_CONTROLLER_IMG_BASE_ADDRESS + addr),
                               chkpkt->data,
                               chkpkt->data_len);
      LOG_PRINTF("chunk: %ld/%ld, write to 0x%lx\n",
                 sl_ctrl_fw_info.chk_id,
                 sl_ctrl_fw_info.chk_tot,
                 addr);
      sl_ctrl_fw_info.chk_id++;
    }
  } else {
    if (sl_ctrl_fw_info.fw_size) {
      uint8_t md5[16] = { 0 };
      calc_md5((uint8_t *) PSRAM_CONTROLLER_IMG_BASE_ADDRESS,
               sl_ctrl_fw_info.fw_size,
               md5);
      LOG_PRINTF("Controller firmware MD5: ");
      sl_print_hex_to_string(md5, 16);
      LOG_PRINTF("\n");
      LOG_PRINTF("Expected MD5: ");
      sl_print_hex_to_string(sl_ctrl_fw_info.md5, 16);
      LOG_PRINTF("\n");
      if (memcmp(md5, sl_ctrl_fw_info.md5, sizeof(sl_ctrl_fw_info.md5)) != 0) {
        ERR_PRINTF("Controller firmware MD5 mismatch.\n");
        return SL_STATUS_FAIL;
      }
      sl_ota_controller_start();
      sl_si91x_soc_nvic_reset();
    } else {
      ERR_PRINTF("Invalid data or length for controller firmware download.\n");
      return SL_STATUS_FAIL;
    }
  }

  return SL_STATUS_OK;
}

/**
 * @brief Start the OTA process on the controller.
 * @return Status code.
 */
sl_status_t sl_ota_controller_start(void)
{
  if (sl_ctrl_fw_info.fw_size == 0) {
    ERR_PRINTF("Controller image size is zero.\n");
    return SL_STATUS_FAIL;
  }

  // Store controller image size
  sl_store_controller_ota_img_size(sl_ctrl_fw_info.fw_size);

  // Start OTA process
  if (sl_start_controller_ota() != SL_STATUS_OK) {
    ERR_PRINTF("Failed to start OTA process.\n");
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

/**
 * @brief Download firmware to a node.
 * @param data Pointer to firmware data.
 * @param len Length of the firmware data.
 * @return Status code.
 */
sl_status_t sl_ota_download_node_fw(void *data, uint16_t len)
{
  if ((len == 0) && !data) {
    ERR_PRINTF("Invalid data or length for controller firmware download.\n");
    return SL_STATUS_FAIL;
  }
  sl_fw_chunk_t *chkpkt = (sl_fw_chunk_t *) data;

  LOG_PRINTF("Received chunk: %d\n", chkpkt->type);
  LOG_PRINTF("Received chunk len: %d\n", len);
  LOG_PRINTF("Chunk data len: %d\n", chkpkt->data_len);

  // Write received data to PSRAM
  if (chkpkt->data_len > 0) {
    if (chkpkt->type == SL_FWUP_RPS_HEADER) {
      sl_node_fw_info.fw_size = chkpkt->data[0] | (chkpkt->data[1] << 8)
                                | (chkpkt->data[2] << 16)
                                | (chkpkt->data[3] << 24);
      uint16_t nodeid = (chkpkt->data[4] << 8) | chkpkt->data[5];
      memcpy(&sl_node_fw_info.md5[0], &chkpkt->data[6], 16);
      sl_node_fw_info.chk_tot =
        ((sl_node_fw_info.fw_size) / CHUNK_SIZE) /*header*/;
      sl_node_fw_info.chk_id = 0;
      LOG_PRINTF("\r\n Image size = %ld, md5: ", sl_node_fw_info.fw_size);
      sl_print_hex_to_string(sl_node_fw_info.md5, 16);
      LOG_PRINTF("\n");
      sl_node_ota_set_fsize(sl_node_fw_info.fw_size, nodeid);
    } else {
      uint32_t addr = sl_node_fw_info.chk_id * CHUNK_SIZE;
      sl_psram_write_auto_mode((PSRAM_NODE_IMG_BASE_ADDRESS + addr),
                               chkpkt->data,
                               chkpkt->data_len);
      LOG_PRINTF("chunk: %ld/%ld, write to 0x%lx\n",
                 sl_node_fw_info.chk_id,
                 sl_node_fw_info.chk_tot,
                 addr);
      sl_node_fw_info.chk_id++;
    }
  } else {
    if (sl_node_fw_info.fw_size) {
      uint8_t md5[16] = { 0 };
      calc_md5((uint8_t *) PSRAM_NODE_IMG_BASE_ADDRESS,
               sl_node_fw_info.fw_size,
               md5);
      LOG_PRINTF("Node firmware MD5: ");
      sl_print_hex_to_string(md5, 16);
      LOG_PRINTF("\n");
      LOG_PRINTF("Expected MD5: ");
      sl_print_hex_to_string(sl_node_fw_info.md5, 16);
      LOG_PRINTF("\n");
      if (memcmp(md5, sl_node_fw_info.md5, sizeof(sl_node_fw_info.md5)) != 0) {
        ERR_PRINTF("node firmware MD5 mismatch.\n");
        return SL_STATUS_FAIL;
      }
      sl_node_ota_setup();
    } else {
      ERR_PRINTF("Invalid data or length for node firmware download.\n");
      return SL_STATUS_FAIL;
    }
  }

  return SL_STATUS_OK;
}

/**
 * @brief Setup the OTA process for a node (calculate chunk, checksum, send request).
 * @return 0 if successful, -1 if error.
 */
int sl_node_ota_setup(void)
{
  if (sl_node_fw_info.fw_size == 0) {
    ERR_PRINTF("ota image size is zero.\n");
    return -1;
  }

  sl_node_fw_info.chk_tot = sl_node_fw_info.fw_size / CHUNK_SIZE;
  sl_node_fw_info.chk_id = 0;

  uint16_t checksum = zgw_crc16(CRC_INIT_VALUE, (uint8_t*)PSRAM_NODE_IMG_BASE_ADDRESS, sl_node_fw_info.fw_size);

  sl_node_ota_send_md_request(checksum); // 0x1602.
  return 0;
}
