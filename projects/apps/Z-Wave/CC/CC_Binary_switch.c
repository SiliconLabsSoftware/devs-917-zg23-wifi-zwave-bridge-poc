/***************************************************************************/ /**
 * @file CC_binary_switch.c
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

#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "ZW_classcmd.h"
#include <stdlib.h>

#include "Net/ZW_udp_server.h"
#include "Common/sl_common_config.h"
#include "Common/sl_common_log.h"

extern ZW_APPLICATION_TX_BUFFER sl_zw_app_txbuf;

/**
 * @brief Handles Z-Wave Binary Switch command class frames.
 *
 * This function processes incoming frames for the Binary Switch Command Class,
 * including SET and REPORT commands, and sends appropriate responses or logs state.
 *
 * @param c      Pointer to the Z-Wave connection structure.
 * @param frame  Pointer to the received frame buffer.
 * @param length Length of the received frame.
 * @return sl_command_handler_codes_t Status code indicating how the command was handled.
 */
sl_command_handler_codes_t sl_cc_bswitch_handler(zwave_connection_t *c,
                                                 uint8_t *frame, uint16_t length)
{
  (void)length; // Mark unused parameter
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)frame;

  LOG_PRINTF("sl_cc_bswitch_handler: cmd (%02X)\n", pCmd->ZW_Common.cmd);

  switch (pCmd->ZW_Common.cmd) {
    case SWITCH_BINARY_SET: {
      ZW_SWITCH_BINARY_SET_FRAME *f = (ZW_SWITCH_BINARY_SET_FRAME*) &sl_zw_app_txbuf;
      f->cmdClass = COMMAND_CLASS_SWITCH_BINARY;
      f->cmd = SWITCH_BINARY_SET;
      f->switchValue = pCmd->ZW_SwitchBinarySetFrame.switchValue;

      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf, sizeof(ZW_SWITCH_BINARY_SET_FRAME),
                          NULL);
      break; //
    }
    case SWITCH_BINARY_REPORT: {
      LOG_PRINTF("Binary switch report state: %X\n", pCmd->ZW_SwitchBinaryReportV2Frame.currentValue);
    }
    break;

    default:
      return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}
