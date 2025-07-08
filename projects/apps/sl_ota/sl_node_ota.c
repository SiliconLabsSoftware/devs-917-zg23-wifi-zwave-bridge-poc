/*******************************************************************************
 * @file  sl_node_ota.c
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

#include "stdio.h"
#include "stdbool.h"
#include "cmsis_os2.h"
#include "Serialapi.h"
#include "lwip/inet.h"
#include "lwip/netif.h"
#include "lwip/if_api.h"
#include "sl_status.h"
#include "sl_uart_drv.h"
#include "sl_controller_ota.h"
#include "sl_common_log.h"
#include "sl_gw_info.h"
#include "modules/sl_psram.h"
#include "utls/zgw_crc.h"
#include "Net/sl_udp_utils.h"
#include "Net/ZW_zip_classcmd.h"
#include "SerialAPI/sl_serial.h"
#include "threads/sl_tcpip_handler.h"
#include "threads/sl_infra_if.h"

#define FW_UPDATE_SEGMENT_SIZE 40
#define FW_TARGET_ID           0x0402

static uint32_t sl_node_fszie = 0;
static uint8_t sl_nodeid_md = 0;

/**
 * @brief Send a Firmware Update Meta Data Request Get (0x7A 0x03) command to the OTA node.
 *
 * @param checksum The firmware checksum (CRC).
 * @return 0 on success, -1 on error.
 */
int sl_node_ota_send_md_request(uint16_t checksum)
{
  ZW_COMMAND_ZIP_PACKET *zippkt;
  uint8_t zipbuf[128];
  uint8_t *zip_payload;
  zippkt            = (ZW_COMMAND_ZIP_PACKET *) zipbuf;
  zippkt->cmdClass  = COMMAND_CLASS_ZIP;
  zippkt->cmd       = COMMAND_ZIP_PACKET;
  zippkt->flags0    = 0x00; // no ack request
  zippkt->flags1    = 0x50; // security enabled.
  zippkt->seqNo     = 0x01; // Example sequence number
  zippkt->sEndpoint = 0;
  zippkt->dEndpoint = 0;
  zip_payload       = zippkt->payload;
  ZW_FIRMWARE_UPDATE_MD_REQUEST_GET_V3_FRAME *fwUpdateMdReqGetV3 =
    (ZW_FIRMWARE_UPDATE_MD_REQUEST_GET_V3_FRAME *) zip_payload;
  fwUpdateMdReqGetV3->cmdClass        = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3;
  fwUpdateMdReqGetV3->cmd             = FIRMWARE_UPDATE_MD_REQUEST_GET_V3;
  fwUpdateMdReqGetV3->manufacturerId1 = 0; // Example manufacturer ID MSB
  fwUpdateMdReqGetV3->manufacturerId2 = 0; // Example manufacturer ID LSB
  fwUpdateMdReqGetV3->firmwareId1 =
    (FW_TARGET_ID >> 8) & 0xFF;   // Example firmware ID MSB
  fwUpdateMdReqGetV3->firmwareId2 =
    FW_TARGET_ID & 0xFF;   // Example firmware ID LSB
  fwUpdateMdReqGetV3->checksum1 =
    (checksum >> 8) & 0xFF;                             // Example checksum MSB
  fwUpdateMdReqGetV3->checksum2      = checksum & 0xFF; // Example checksum LSB
  fwUpdateMdReqGetV3->firmwareTarget = 0; // Example firmware target
  fwUpdateMdReqGetV3->fragmentSize1 =
    (FW_UPDATE_SEGMENT_SIZE >> 8) & 0xFF;   // Example fragment size MSB
  fwUpdateMdReqGetV3->fragmentSize2 =
    FW_UPDATE_SEGMENT_SIZE & 0xFF;   // Example fragment size LSB
  // Send the Z-Wave command
  uint16_t pktlen = sizeof(ZW_COMMAND_ZIP_PACKET)
                    + sizeof(ZW_FIRMWARE_UPDATE_MD_REQUEST_GET_V3_FRAME);
  struct in6_addr in;
  memcpy(in.un.u8_addr, router_cfg.unsolicited_dest.u8, 16);
  char addr_str[INET6_ADDRSTRLEN];
  snprintf(addr_str, sizeof(addr_str), "%s%x", SL_INFRA_IF_RIO_PREFIX, sl_nodeid_md);
  sl_tcpip_buf_t *tcpipzip = sl_zip_packet_v6(
    &in,
    router_cfg.unsolicited_port,
    addr_str,   // Local IP address, can be NULL for default
    zipbuf,
    pktlen);

  if (tcpipzip) {
    LOG_PRINTF("SEND MD REQUEST: \n");
    sl_print_hex_buf(zipbuf, pktlen);
    zw_tcpip_post_event(1, tcpipzip);
  } else {
    ERR_PRINTF("OMG NO MEM!\n");
    return -1;
  }

  return 0;
}

/**
 * @brief Send a firmware chunk packet to the OTA node.
 *
 * @param data Pointer to the chunk data buffer.
 * @param dlen Length of the chunk data.
 * @param pros Chunk properties (bit 7 = 1 if last chunk).
 * @param ch_id Chunk sequence number.
 * @return 0 on success, -1 on error.
 */
int sl_node_ota_send_md_packet(uint8_t *data,
                               uint16_t dlen,
                               uint8_t pros,
                               uint16_t ch_id)
{
  ZW_COMMAND_ZIP_PACKET *zippkt;
  uint8_t zipbuf[128];
  uint8_t *zip_payload;
  zippkt            = (ZW_COMMAND_ZIP_PACKET *) zipbuf;
  zippkt->cmdClass  = COMMAND_CLASS_ZIP;
  zippkt->cmd       = COMMAND_ZIP_PACKET;
  zippkt->flags0    = 0x00;           // no ack request
  zippkt->flags1    = 0x50;           // security enabled.
  zippkt->seqNo     = (ch_id & 0xFF); // Example sequence number
  zippkt->sEndpoint = 0;
  zippkt->dEndpoint = 0;
  zip_payload       = zippkt->payload;
  ZW_FIRMWARE_UPDATE_MD_REPORT_1BYTE_V4_FRAME *fw_chunk =
    (ZW_FIRMWARE_UPDATE_MD_REPORT_1BYTE_V4_FRAME *)
    zip_payload;       // Send the Z-Wave command
  fw_chunk->cmdClass = COMMAND_CLASS_FIRMWARE_UPDATE_MD_V3;
  fw_chunk->cmd      = FIRMWARE_UPDATE_MD_REPORT;
  fw_chunk->properties1 =
    pros | ((ch_id >> 8) & 0xFF);           // 0 normal, 0x80 last chunk
  fw_chunk->reportNumber2 = (ch_id & 0xFF); // Example report number
  memcpy(&fw_chunk->data1, data, dlen);     // Copy the data into the frame

  uint16_t checksum = zgw_crc16(CRC_INIT_VALUE, zip_payload, dlen + 4);
  *(&fw_chunk->data1 + dlen)     = (checksum >> 8) & 0xFF;
  *(&fw_chunk->data1 + dlen + 1) = checksum & 0xFF; // Example checksum LSB

  uint16_t pktlen = sizeof(ZW_COMMAND_ZIP_PACKET) - 1 + 4   // for md header.
                    + dlen + 2;                         // 2 for checksum
  struct in6_addr in;
  memcpy(in.un.u8_addr, router_cfg.unsolicited_dest.u8, 16);
  char addr_str[INET6_ADDRSTRLEN];
  snprintf(addr_str, sizeof(addr_str), "%s%x", SL_INFRA_IF_RIO_PREFIX, sl_nodeid_md);

  sl_tcpip_buf_t *tcpipzip = sl_zip_packet_v6(
    &in,
    router_cfg.unsolicited_port,
    addr_str,   // Local IP address, can be NULL for default
    zipbuf,
    pktlen);

  if (tcpipzip) {
    LOG_PRINTF("SEND MD packet: %d\n", ch_id);
    zw_tcpip_post_event(1, tcpipzip);
  } else {
    ERR_PRINTF("OMG NO MEM!\n");
    return -1;
  }

  return 0;
}

/**
 * @brief Send multiple firmware chunk packets to the OTA node.
 *
 * @param ch_id Starting chunk sequence number (starts from 1).
 * @param ch_num Number of chunks to send.
 * @return 0 on success, -1 on error.
 */
int sl_node_ota_send_md_chunks(uint16_t ch_id, uint16_t ch_num)
{
  // ch_id: start from 1.
  uint32_t addr                        = PSRAM_NODE_IMG_BASE_ADDRESS;
  uint8_t data[FW_UPDATE_SEGMENT_SIZE] = { 0 };
  uint32_t size;
  uint8_t chunk_size = FW_UPDATE_SEGMENT_SIZE;
  uint8_t pros       = 0;

  if (ch_id == 0) {
    ERR_PRINTF("Wrong chunk id\n");
    return 0;
  }

  addr = PSRAM_NODE_IMG_BASE_ADDRESS + ((ch_id - 1) * FW_UPDATE_SEGMENT_SIZE);
  uint16_t next = ch_id + ch_num;
  for (int i = ch_id; i < next; i++) {
    if (pros == 0x80) {
      LOG_PRINTF("Last chunk done. wait zw network\n");
      break;
    }
    LOG_PRINTF("read MD packet: %d\n", i);

    size = i * FW_UPDATE_SEGMENT_SIZE;
    if (size > sl_node_fszie) {
      chunk_size = sl_node_fszie % FW_UPDATE_SEGMENT_SIZE;
      pros       = 0x80;
    }

    sl_psram_read_auto_mode(addr, data, chunk_size);
    if (sl_node_ota_send_md_packet(data, chunk_size, pros, i) != 0) {
      ERR_PRINTF("Failed to send firmware chunk.\n");
      return -1;
    }
    addr += FW_UPDATE_SEGMENT_SIZE;
  }
  return 0;
}

/**
 * @brief Set the firmware size and node ID for the OTA process.
 *
 * @param s Firmware size in bytes.
 * @param nodeid Node ID of the OTA target.
 */
void sl_node_ota_set_fsize(uint32_t s, uint8_t nodeid)
{
  if (s == 0 || nodeid == 0) {
    ERR_PRINTF("Invalid firmware size: %ld or node %d\n", s, nodeid);
    return;
  }
  LOG_PRINTF("FW: %ld, n: %d\n", s, nodeid);
  sl_node_fszie = s;
  sl_nodeid_md = nodeid;
}
