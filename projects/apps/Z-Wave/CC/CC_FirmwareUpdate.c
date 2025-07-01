/* © 2019 Silicon Laboratories Inc. */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>

#include <FreeRTOS.h>
#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "ZW_classcmd.h"

#include "Common/sl_common_log.h"
#include "Common/sl_gw_info.h"
#include "utls/ipv6_utils.h"
#include "utls/zgw_crc.h"
#include "Net/ZW_udp_server.h"
#include "Net/sl_udp_utils.h"
#include "ip_bridge/sl_bridge_temp_assoc.h"
#include "sl_ota/sl_node_ota.h"

#include "sl_sleeptimer.h"

#include "CC_FirmwareUpdate.h"

static sl_command_handler_codes_t sli_md_get_v3_handler(zwave_connection_t *c,
                                                        uint8_t *pData,
                                                        uint16_t bDatalen)
{
  (void) c;
  const ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME *f =
    (ZW_FIRMWARE_UPDATE_MD_GET_V3_FRAME *) pData;

  sl_print_hex_buf(pData, bDatalen);
  uint16_t num   = f->numberOfReports;
  uint16_t ch_id = ((f->properties1 & 0x7F) << 8) | f->reportNumber2;
  sl_node_ota_send_md_chunks(ch_id, num);

  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_md_update_md_status_report_handler(zwave_connection_t *c,
                                       uint8_t *pData,
                                       uint16_t bDatalen)
{
  (void) c;
  ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V4_FRAME *f =
    (ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V4_FRAME *) pData;

  if (bDatalen < sizeof(ZW_FIRMWARE_UPDATE_MD_STATUS_REPORT_V4_FRAME)) {
    return COMMAND_PARSE_ERROR;
  }

  if (f->status == 0xFF) {
    LOG_PRINTF("Firmware update successful, status = 0x%02x\r\n", f->status);
  } else {
    LOG_PRINTF("Firmware update failed, status = 0x%02x\r\n", f->status);
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t sli_md_request_report_v3_handler(zwave_connection_t *c,
                                                                   uint8_t *pData,
                                                                   uint16_t bDatalen)
{
  (void) c;
  sl_print_hex_buf(pData, bDatalen);
  ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME *f =
    (ZW_FIRMWARE_UPDATE_MD_REQUEST_REPORT_V3_FRAME *) pData;
  if (f->status == 0xFF) {
    LOG_PRINTF("Firmware Valid combination\n");
  } else {
    LOG_PRINTF("Firmware not good, status = %u\r\n", f->status);
  }
  return COMMAND_HANDLED;
}

/*
 *  Firmware update command class handler
 *  Returns TRUE if received command is supported command.
 *  Returns FALSE if received command is non-supported command(e.g. meta data get and firmware update status)
 */
sl_command_handler_codes_t Fwupdate_Md_CommandHandler(zwave_connection_t *c,
                                                      uint8_t *pData,
                                                      uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *) pData;

  DBG_PRINTF("Fwupdate_Md_CommandHandler: pCmd->ZW_Common.cmd = %02x \n",
             pCmd->ZW_Common.cmd);

  switch (pCmd->ZW_Common.cmd) {
    case FIRMWARE_MD_REPORT:
      ZW_FIRMWARE_MD_REPORT_FRAME *f = (ZW_FIRMWARE_MD_REPORT_FRAME*) pData;
      LOG_PRINTF("manufacturerId: %2X%2X\nfirmwareId: %2X%2X\nchecksum1: %2X%2X\n",
                 f->manufacturerId1, f->manufacturerId2,
                 f->firmwareId1, f->firmwareId2,
                 f->checksum1, f->checksum2);
      break;
    case FIRMWARE_UPDATE_MD_REQUEST_REPORT:
      sli_md_request_report_v3_handler(c, pData, bDatalen);
      break;

    case FIRMWARE_UPDATE_MD_GET_V3:
      return sli_md_get_v3_handler(c, pData, bDatalen);
      break;

    case FIRMWARE_UPDATE_MD_STATUS_REPORT_V4:
      sli_md_update_md_status_report_handler(c, pData, bDatalen);
      break;
    default:
      LOG_PRINTF("This FW update command is not for the GW. cmd = 0x%02x "
                 "bDatalen = %u \r\n",
                 pCmd->ZW_Common.cmd,
                 bDatalen);
      return COMMAND_NOT_SUPPORTED;
      break;
  }

  return COMMAND_HANDLED;
}
