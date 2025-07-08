/***************************************************************************/ /**
 * @file sl_common_type.h
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

#ifndef SL_COMMON_TYPE_H
#define SL_COMMON_TYPE_H

#include <stdint.h>

#define SL_USE_LWIP_STACK 1

typedef uint8_t u8_t;
typedef int8_t s8_t;

typedef int16_t s16_t;
typedef uint16_t u16_t;

typedef uint32_t clock_time_t;

#define clock_time osKernelGetTickCount

typedef enum {
  COMMAND_HANDLED, ///< the command was handled by the command handler. Supervision returns SUCCESS
  COMMAND_PARSE_ERROR, ///< the command handler was unable to parse the command. Supervision returns FAIL
  COMMAND_BUSY, ///< the command handler was unable to process to request because it is currently performing another operation. Supervision returns FAIL
  COMMAND_NOT_SUPPORTED, ///< the command handler does not support this command. Supervision returns. Supervision returns NO_SUPPORT
  CLASS_NOT_SUPPORTED, ///< there is no command handler registered for this class. This is an internal return code and should not be returned by any registered handler. Supervision returns NO_SUPPORT
  COMMAND_POSTPONED, ///< the command has been postponed  because the receiver is a sleeping device
  COMMAND_CLASS_DISABLED, ///< Command class has been disabled. This is an internal return code and should not be returned by any registered handler. Supervision returns NO_SUPPORT
} sl_command_handler_codes_t;

typedef struct {
  uint32_t ev;
  void *ev_data;
} sl_cc_net_ev_t;

typedef struct {
  uint8_t cmd;
  uint8_t data[1];
} __attribute__((packed)) sl_srv_data_t;

#define SL_SRV_NODE_CTRL_COMMAND        0x10
#define SL_SRV_OTA_BRIDGE_COMMAND       0x20
#define SL_SRV_OTA_CONTROLLER_COMMAND   0x30
#define SL_SRV_OTA_NODE_COMMAND         0x40

#define SL_WIFI_SSID_BUF_SIZE 50
#define SL_WIFI_PWD_BUF_SIZE  50
/**
 * @brief Event system message structure.
 *
 * This structure represents a message in the event system, containing the event ID,
 * length of the event data, and a pointer to the event data.
 */
typedef struct {
  uint32_t event_id;          ///< Event ID
  uint32_t event_data_length; ///< The length of event_data
  void *event_data;           ///< Pointer to event data
} __attribute__((packed)) sl_event_system_msg_t;

typedef struct {
  uint8_t ssid[SL_WIFI_SSID_BUF_SIZE];
  uint8_t pwd[SL_WIFI_PWD_BUF_SIZE];
  uint8_t sec_type;
} sl_ap_t;

typedef sl_event_system_msg_t sl_ble_event_t;
typedef sl_event_system_msg_t sl_wlan_event_t;
typedef sl_event_system_msg_t sl_btn_event_t;

#endif // SL_COMMON_TYPE_H
