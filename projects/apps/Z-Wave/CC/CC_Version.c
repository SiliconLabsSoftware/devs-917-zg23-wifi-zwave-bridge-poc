/***************************************************************************/ /**
 * @file sl_cc_version.c
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
#include <stdlib.h>
#include "Serialapi.h"
#include "Z-Wave/include/ZW_classcmd_ex.h"
#include "Z-Wave/include/ZW_classcmd.h"

#include "Net/ZW_udp_server.h"
#include "Common/sl_common_config.h"
#include "Common/sl_common_log.h"

extern ZW_APPLICATION_TX_BUFFER sl_zw_app_txbuf;

#define NUM_FW_TARGETS \
  1 /* Number of additional FW targets reported in Version Report */

/**
 * @brief Handles Z-Wave version command class frames.
 *
 * This function processes incoming frames for the version Command Class
 *
 * @param c      Pointer to the Z-Wave connection structure.
 * @param frame  Pointer to the received frame buffer.
 * @param length Length of the received frame.
 * @return sl_command_handler_codes_t Status code indicating how the command was handled.
 */
sl_command_handler_codes_t VersionHandler(zwave_connection_t *c,
                                          uint8_t *frame, uint16_t length)
{
  (void)length; // Đánh dấu tham số không sử dụng để tránh lỗi biên dịch
  char buf[64];
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)frame;
  LOG_PRINTF("VersionHandler: cmd (%02X)\n", pCmd->ZW_Common.cmd);

  switch (pCmd->ZW_Common.cmd) {
    case VERSION_GET: {
      ZW_VERSION_REPORT_2BYTE_V2_FRAME *f =
        (ZW_VERSION_REPORT_2BYTE_V2_FRAME *)&sl_zw_app_txbuf;
      f->cmdClass = COMMAND_CLASS_VERSION;
      f->cmd = VERSION_REPORT;
      f->zWaveLibraryType = ZW_Version((BYTE *)buf);

      f->zWaveProtocolVersion = atoi(buf + 7);
      f->zWaveProtocolSubVersion = atoi(buf + 9);

      Get_SerialAPI_AppVersion(&f->firmware0Version, &f->firmware0SubVersion);

      f->numberOfFirmwareTargets = NUM_FW_TARGETS;
      f->hardwareVersion = 1;
      /* Gateway version */
      f->variantgroup1.firmwareVersion = 0;
      f->variantgroup1.firmwareSubVersion = 0;

      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf, sizeof(ZW_VERSION_REPORT_2BYTE_V2_FRAME),
                          NULL);
      break; // VERSION_GET
    }
    case VERSION_REPORT:
      ZW_VERSION_REPORT_FRAME *f =
        (ZW_VERSION_REPORT_FRAME *)&pCmd->ZW_VersionReportFrame;
      LOG_PRINTF("FW version: %d.%d\n", f->applicationVersion,
                 f->applicationSubVersion);
      break;
    case VERSION_COMMAND_CLASS_GET:
      /*If asked non-secre only answer on what we support secure*/

      sl_zw_app_txbuf.ZW_VersionCommandClassReportFrame.cmdClass = COMMAND_CLASS_VERSION;
      sl_zw_app_txbuf.ZW_VersionCommandClassReportFrame.cmd = VERSION_COMMAND_CLASS_REPORT;
      sl_zw_app_txbuf.ZW_VersionCommandClassReportFrame.requestedCommandClass =
        pCmd->ZW_VersionCommandClassGetFrame.requestedCommandClass;

      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf,
                          sizeof(sl_zw_app_txbuf.ZW_VersionCommandClassReportFrame), NULL);
      break; // VERSION_COMMAND_CLASS_GET

    case VERSION_CAPABILITIES_GET:
      DBG_PRINTF("VERSION_CAPABILITIES_GET\n");

      ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME *fc =
        (ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME *)&sl_zw_app_txbuf;
      fc->cmdClass = COMMAND_CLASS_VERSION;
      fc->cmd = VERSION_CAPABILITIES_REPORT;
      fc->properties = 0x07;
      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf,
                          sizeof(ZW_VERSION_CAPABILITIES_REPORT_V3_FRAME), NULL);
      break;
    case VERSION_ZWAVE_SOFTWARE_GET:
      DBG_PRINTF("VERSION_ZWAVE_SOFTWARE_GET\n");

      PROTOCOL_VERSION zw_protocol_version = { 0 };
      ZW_GetProtocolVersion(&zw_protocol_version);

      ZW_Version((BYTE *)buf);      /* Protocol version */

      ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME *fz =
        (ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME *)&sl_zw_app_txbuf;
      fz->cmdClass = COMMAND_CLASS_VERSION;
      fz->cmd = VERSION_ZWAVE_SOFTWARE_REPORT;
      fz->sDKversion1 = 0;
      fz->sDKversion2 = 0;
      fz->sDKversion3 = 0;
      fz->applicationFrameworkAPIVersion1 = 0; // (MSB)
      fz->applicationFrameworkAPIVersion2 = 0;
      fz->applicationFrameworkAPIVersion3 = 0; // (LSB)
      fz->applicationFrameworkBuildNumber1 = 0; // (MSB)
      fz->applicationFrameworkBuildNumber2 = 0; // (LSB)
      fz->hostInterfaceVersion1 = zw_protocol_version.protocolVersionMajor;
      fz->hostInterfaceVersion2 = zw_protocol_version.protocolVersionMinor;
      fz->hostInterfaceVersion3 = zw_protocol_version.protocolVersionRevision;
      fz->hostInterfaceBuildNumber1 = 0;       // (MSB)
      fz->hostInterfaceBuildNumber2 = 0;       // (LSB)
      fz->zWaveProtocolVersion1 = atoi(buf + 7); // (MSB)
      fz->zWaveProtocolVersion2 = atoi(buf + 9);
      fz->zWaveProtocolVersion3 = 0;   // (LSB)
      fz->zWaveProtocolBuildNumber1 = 0; // (MSB) /*TODO: How to get this?*/
      fz->zWaveProtocolBuildNumber2 = 0; // (LSB)
      fz->applicationVersion1 = 0;     //(MSB)
      fz->applicationVersion2 = 0;
      fz->applicationVersion = 0;    // (LSB)
      fz->applicationBuildNumber1 = 0; //(MSB)
      fz->applicationBuildNumber2 = 0; //(LSB)
      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf,
                          sizeof(ZW_VERSION_ZWAVE_SOFTWARE_REPORT_V3_FRAME), NULL);
      break;
    default:
      return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}
