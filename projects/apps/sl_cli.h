/***************************************************************************/ /**
 * @file sl_cli.h
 * @brief CLI functions
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

#ifndef SL_CLI_H
#define SL_CLI_H

#define CONSOLE_TYPE(name) CONSOLE_##name##_TYPE

// OTA Arguments
#define OTA_BRIDGE        0
#define OTA_CONTROLLER    1
#define OTA_END_DEVICE    2
#define OTA_STOP          3

typedef enum {
  CONSOLE_TYPE(ota_options),
  CONSOLE_TYPE_COUNT // Equals the number of different types
} console_type_t;

/***************************************************************************/ /**
 * Initialize cli.
 ******************************************************************************/
void sl_cli_init();

#endif //SL_CLI_H
