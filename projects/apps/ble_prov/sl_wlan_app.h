/***************************************************************************/ /**
 * @file sl_wlan_app.h
 * @brief Top level application functions
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

#ifndef SL_WLAN_APP_H
#define SL_WLAN_APP_H

// Enumeration for states in application
typedef enum wifi_app_state_e {
  WIFI_APP_INITIAL_STATE             = 0,
  WIFI_APP_UNCONNECTED_STATE         = 1,
  WIFI_APP_CONNECTED_STATE           = 2,
  WIFI_APP_IPCONFIG_DONE_STATE       = 3,
  WIFI_APP_SCAN_STATE                = 4,
  WIFI_APP_JOIN_STATE                = 5,
  WIFI_APP_SOCKET_RECEIVE_STATE      = 6,
  WIFI_APP_MQTT_INIT_DONE_STATE      = 7,
  WIFI_APP_MQTT_SUBSCRIBE_DONE_STATE = 8,
  BLE_APP_GATT_WRITE_EVENT           = 9,
  WIFI_APP_DISCONNECTED_STATE        = 10,
  WIFI_APP_DISCONN_NOTIFY_STATE      = 11,
  WIFI_APP_ERROR_STATE               = 12,
  WIFI_APP_FLASH_STATE               = 13,
  WIFI_APP_DATA_RECEIVE_STATE        = 14,
  WIFI_APP_SD_WRITE_STATE            = 15,
  WIFI_APP_DEMO_COMPLETE_STATE       = 16
} wifi_app_state_t;

typedef enum wifi_app_cmd_e {
  WIFI_APP_DATA                 = 0,
  WIFI_APP_SCAN_RESP            = 1,
  WIFI_APP_CONNECTION_STATUS    = 2,
  WIFI_APP_DISCONNECTION_STATUS = 3,
  WIFI_APP_DISCONNECTION_NOTIFY = 4,
  WIFI_APP_TIMEOUT_NOTIFY       = 5
} wifi_app_cmd_t;

#define SL_WLAN_STATUS_CONNECTED    1
#define SL_WLAN_STATUS_DISCONNECTED 0

typedef void (*sl_wlan_event_handler_t)(sl_wlan_event_t *evt);
typedef struct {
  uint32_t event_id;
  sl_wlan_event_handler_t cb;
} sl_wlan_event_manage_t;

typedef struct {
  sl_wifi_scan_result_t *sl_wlan_scan_result;
  sl_wifi_client_configuration_t access_point;
  sl_net_ip_configuration_t ip_address;
  volatile bool sl_wlan_scan_completed;
  volatile sl_status_t sl_wlan_scan_callback_status;
  uint8_t sl_wlan_connect_status;
  uint8_t sl_wlan_disassosiated;
  uint8_t sl_count_try_connect;
} sl_wlan_inst_t;

/*==============================================*/
/**
 * @fn         sl_wlan_app_set_event
 * @brief      sets the specific event.
 * @param[in]  event_num, specific event number.
 * @return     none.
 * @section description
 * This function is used to set/raise the specific event.
 */
sl_status_t
sl_wlan_app_set_event(uint32_t event_num, uint8_t *data, uint32_t data_len);

/*==============================================*/
/**
 * @fn         sl_wlan_connect_status
 * @brief      get status of wlan instant
 * @return     status, specific status of wlan..
 */
uint32_t sl_wlan_connect_status(void);

/*==============================================*/
/**
 * @fn         sl_wlan_init
 * @brief      init wlan
 * @return     none
 */
void sl_wlan_init(void);

#endif // SL_WLAN_APP_H
