/***************************************************************************/ /**
 * @file sl_cc_irrigation.c
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

#define COMMAND_CLASS_IRRIGATION 0x6B
#define IRRIGATION_SHUTOFF 0x01
#define IRRIGATION_RUN 0x02

extern ZW_APPLICATION_TX_BUFFER sl_zw_app_txbuf;

/**
 * @brief Handles Z-Wave Irrigation command class frames.
 *
 * This function processes incoming frames for the Irrigation Command Class,
 * including SHUTOFF and RUN commands, and sends appropriate responses.
 *
 * @param c      Pointer to the Z-Wave connection structure.
 * @param frame  Pointer to the received frame buffer.
 * @param length Length of the received frame.
 * @return sl_command_handler_codes_t Status code indicating how the command was handled.
 */
sl_command_handler_codes_t sl_cc_irrigation_handler(zwave_connection_t *c,
                                                    uint8_t *frame, uint16_t length)
{
  (void)length; // Mark unused parameter
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *)frame;

  LOG_PRINTF("sl_cc_irrigation_handler: cmd (%02X)\n", pCmd->ZW_Common.cmd);

  switch (pCmd->ZW_Common.cmd) {
    case IRRIGATION_SHUTOFF: {
      ZW_IRRIGATION_SHUTOFF_FRAME *f =
        (ZW_IRRIGATION_SHUTOFF_FRAME *)&sl_zw_app_txbuf;
      f->cmdClass =  COMMAND_CLASS_IRRIGATION;
      f->cmd = IRRIGATION_SHUTOFF;
      f->duration = pCmd->ZW_SwitchBinarySetFrame.switchValue;
      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf, sizeof(ZW_IRRIGATION_SHUTOFF_FRAME),
                          NULL);
      break; // IRRIGATION_SHUTOFF
    }
    case IRRIGATION_RUN:
      /*If asked non-secre only answer on what we support secure*/
      ZW_IRRIGATION_RUN_FRAME *f =
        (ZW_IRRIGATION_RUN_FRAME *)&sl_zw_app_txbuf;
      f->cmdClass = COMMAND_CLASS_IRRIGATION;
      f->cmd = IRRIGATION_RUN;
      // using doorlock config the same packet of irrigation command run.
      f->master = pCmd->ZW_DoorLockConfigurationSetFrame.operationType;
      f->valve_id = pCmd->ZW_DoorLockConfigurationSetFrame.properties1;
      f->duration_b0 = pCmd->ZW_DoorLockConfigurationSetFrame.lockTimeoutMinutes;
      f->duration_b1 = pCmd->ZW_DoorLockConfigurationSetFrame.lockTimeoutSeconds;

      sl_zw_send_zip_data(c, (BYTE *)&sl_zw_app_txbuf,
                          sizeof(ZW_IRRIGATION_RUN_FRAME), NULL);
      break; // IRRIGATION_RUN

    default:
      return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}
