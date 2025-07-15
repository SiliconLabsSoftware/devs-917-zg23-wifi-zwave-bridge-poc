/***************************************************************************/ /**
 * @file sl_serial_api_handler.c
 * @brief USART driver function
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

#include <FreeRTOS.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>
#include "SerialAPI/Serialapi.h"
#include "rsi_common_apis.h"
#include "sl_common_type.h" // Standard types
#include "sl_common_log.h"

#include "sl_uart_drv.h"
#include "sl_serial.h"
#include "Serialapi.h"
#include "sl_zw_router.h"
#include "sl_security_layer.h"

#define SL_SAPI_QUEUE_NUMBER 10

const osThreadAttr_t sl_sapi_thread_attributes = {
  .name       = "sapi_t",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 6120,
  .priority   = osPriorityNormal7,
  .tz_module  = 0,
  .reserved   = 0,
};

const struct SerialAPI_Callbacks serial_api_callbacks = {
  sl_application_serial_handler,
  0,
  sl_appl_controller_update,
  0,
  0,
  0,
  0,
  sl_application_serial_handler,
  sl_serial_api_started,
};
PROTOCOL_VERSION zw_protocol_version2 = { 0 };
struct chip_descriptor chip_desc      = { CHIP_DESCRIPTOR_UNINITIALIZED,
                                          CHIP_DESCRIPTOR_UNINITIALIZED };

static bool sli_enter_ota_mode = false;

void sli_serila_validates(void);
void sl_serial_start(void);
bool sl_serial_api_is_ota_mode(void);
void sl_serial_api_enter_ota_mode(bool state);

static void sli_sapi_thread_handler(void *arg)
{
  (void) arg;
  // init something.
  SL_LOG_PRINT("task serial api\n");
  sl_serial_start();

  while (1) {
    if (sl_serial_api_is_ota_mode()) {
      // In OTA mode, we don't poll the serial API.
      // We just wait for the OTA handler to do its job.
      osDelay(100);
      continue;
    }

    SerialAPI_Poll();
    // security poll here
    secure_poll();

    osDelay(1); // delay 1ms.
  }
}

void sl_serial_api_enter_ota_mode(bool state)
{
  // Initialize the serial API handler thread.
  sli_enter_ota_mode = state;
}

bool sl_serial_api_is_ota_mode(void)
{
  return sli_enter_ota_mode == true;
}

void sl_serial_start(void)
{
  int ret;
  ret = SerialAPI_Init("uart0", &serial_api_callbacks);
  if (!ret) {
    SL_LOG_PRINT("Serial API init error!");
  }

  sli_serila_validates();
}

void sli_serila_validates(void)
{
  ZW_GetProtocolVersion(&zw_protocol_version2);
  SL_LOG_PRINT("SDK: %d.%02d.%02d, SDK Build no: %d\n",
               zw_protocol_version2.protocolVersionMajor,
               zw_protocol_version2.protocolVersionMinor,
               zw_protocol_version2.protocolVersionRevision,
               zw_protocol_version2.zaf_build_no);
  SL_LOG_PRINT(" SDK git hash: ");
  for (int i = 0; i < 16; i++) {
    SL_LOG_PRINT("%x", zw_protocol_version2.git_hash_id[i]);
  }
  SL_LOG_PRINT("\n");

  /* Abort if we find out that the Z-Wave module is not running a bridge library */

  /* RFRegion and TXPowerlevel requires Z-Wave module restarts */
  SerialAPI_GetChipTypeAndVersion(&(chip_desc.my_chip_type),
                                  &(chip_desc.my_chip_version));
  /*
   * Set the TX powerlevel only if
   * 1. If it's not 500 series
   * 2. If the module supports the command and sub-command
   * 3. Valid powerlevel setting exists in zipgateway.cfg
   * 4. Current settings != settings in zipgateway.cfg
   */
  if (chip_desc.my_chip_type == ZW_CHIP_TYPE) {
    SL_LOG_PRINT("The module is 500 series. Ignoring NormalTxPowerLevel, "
                 "Measured0dBmPower, ZWRFRegion and MaxLRTxPowerLevel from "
                 "config file.\n");
    goto exit;
  }

  if (!SerialAPI_SupportsCommand_func(FUNC_ID_SERIALAPI_SETUP)
      || !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_TX_POWERLEVEL_GET)
      || !SupportsSerialAPISetup_func(SERIAL_API_SETUP_CMD_TX_POWERLEVEL_SET)) {
    SL_LOG_PRINT(
      "The module does not support FUNC_ID_SERIALAPI_SETUP,"
      "SERIAL_API_SETUP_CMD_TX_POWERLEVEL_GET or "
      "SERIAL_API_SETUP_CMD_TX_POWERLEVEL_SET. Ignoring NormalTxPowerLevel,"
      "Measured0dBmPower from config file\n");
    goto skip_setting_power_level;
  }

  skip_setting_power_level:

  exit:
  if (ZW_RFRegionGet() == RF_US_LR) {
    if (SerialAPI_EnableLR() == false) {
      SL_LOG_PRINT("Failed to enable Z-Wave Long Range capability\n");
    }
  } else {
    if (SerialAPI_DisableLR() == false) {
      SL_LOG_PRINT("Fail to disable Z-Wave Long Range capability\n");
    }
  }
  ZW_AddNodeToNetwork(ADD_NODE_STOP, 0);
  ZW_RemoveNodeFromNetwork(REMOVE_NODE_STOP, 0);
  ZW_SetLearnMode(ZW_SET_LEARN_MODE_DISABLE, 0);

  if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
    SerialAPI_WatchdogStart();
  }
}

void sl_serial_api_init(void)
{
  osThreadNew((osThreadFunc_t) sli_sapi_thread_handler,
              NULL,
              &sl_sapi_thread_attributes);
}
