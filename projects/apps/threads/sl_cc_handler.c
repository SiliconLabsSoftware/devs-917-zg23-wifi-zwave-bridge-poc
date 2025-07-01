/***************************************************************************/ /**
 * @file sl_cc_handler.c
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

#include "sl_cc_handler.h"
#include "Z-Wave/include/ZW_classcmd.h"
#include "Z-Wave/include/ZW_classcmd_ex.h"

#include "Z-Wave/CC/CC_Irrigation.h"
#include "Z-Wave/CC/CC_Version.h"
#include "Z-Wave/CC/CC_Binary_switch.h"
#include "Z-Wave/CC/CC_InclusionController.h"
#include "Z-Wave/CC/CC_NetworkManagement.h"
#include "Z-Wave/CC/CC_FirmwareUpdate.h"

// to register command handler, please add to this table
const command_handler_t sl_cc_handler_table[] = {
  { .handler        = sl_cc_irrigation_handler,
    .cmdClass       = COMMAND_CLASS_IRRIGATION,
    .init           = NULL,
    .minimal_scheme = NO_SCHEME,
    .version        = IRRIGATION_VERSION },
  { .handler        = VersionHandler,
    .cmdClass       = COMMAND_CLASS_VERSION,
    .init           = NULL,
    .minimal_scheme = NET_SCHEME,
    .version        = VERSION_VERSION_V3 },
  { .handler        = sl_cc_bswitch_handler,
    .cmdClass       = COMMAND_CLASS_SWITCH_BINARY,
    .init           = NULL,
    .minimal_scheme = NO_SCHEME,
    .version        = 1 },
  { .handler        = inclusuion_controller_handler,
    .cmdClass       = COMMAND_CLASS_INCLUSION_CONTROLLER,
    .init           = &controller_inclusuion_init,
    .minimal_scheme = NO_SCHEME,
    .version        = 1 },
  { .handler        = NetworkManagementCommandHandler,
    .cmdClass       = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC,
    .init           = NULL,
    .minimal_scheme = SECURITY_SCHEME_0,
    .version        = NETWORK_MANAGEMENT_BASIC_VERSION_V2 },
  { .handler        = NetworkManagementCommandHandler,
    .cmdClass       = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY,
    .init           = NULL,
    .minimal_scheme = SECURITY_SCHEME_0,
    .version        = NETWORK_MANAGEMENT_PROXY_VERSION_V4 },
  { .handler        = NetworkManagementCommandHandler,
    .cmdClass       = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION,
    .init           = NULL,
    .minimal_scheme = SECURITY_SCHEME_0,
    .version        = NETWORK_MANAGEMENT_INCLUSION_VERSION_V4 },
  { .handler        = Fwupdate_Md_CommandHandler,
    .cmdClass       = COMMAND_CLASS_FIRMWARE_UPDATE_MD,
    .init           = NULL,
    .minimal_scheme = SECURITY_SCHEME_0,
    .version        = FIRMWARE_UPDATE_MD_VERSION_V5 },
};

#define SL_CC_HANDLER_TABLE_LENGTH \
  sizeof(sl_cc_handler_table) / sizeof(sl_cc_handler_table[0])

sl_command_handler_codes_t sl_cc_handler_run(zwave_connection_t *connection,
                                             uint8_t *payload,
                                             uint16_t len,
                                             uint8_t bSupervisionUnwrapped)
{
  (void) bSupervisionUnwrapped;

  for (unsigned int i = 0; i < SL_CC_HANDLER_TABLE_LENGTH; i++) {
    if (sl_cc_handler_table[i].cmdClass == payload[0]) {
      return sl_cc_handler_table[i].handler(connection, payload, len);
    }
  }

  return CLASS_NOT_SUPPORTED;
}

uint8_t ZW_comamnd_handler_version_get(security_scheme_t scheme,
                                       uint16_t cmdClass)
{
  (void) scheme;
  const command_handler_t *fn;

  for (unsigned int i = 0; i < SL_CC_HANDLER_TABLE_LENGTH; i++) {
    fn = &sl_cc_handler_table[i];
    if (fn->cmdClass == cmdClass) {
      return fn->version;
    }
  }
  return 0x00; //UNKNOWN_VERSION;;
}
