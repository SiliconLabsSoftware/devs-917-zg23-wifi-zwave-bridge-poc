/*******************************************************************************
 * @file  sl_ble_app.c
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
#include "FreeRTOS.h"
#include "sl_board_configuration.h"
#include "sl_constants.h"
#include "sl_wifi.h"
#include "sl_net_ip_types.h"
#include "cmsis_os2.h"
#include "sl_utility.h"

// BLE include file to refer BLE APIs
#include <rsi_ble_apis.h>
#include <rsi_bt_common_apis.h>
#include <rsi_common_apis.h>
#include <string.h>
#include "sl_status.h"

#include "sl_common_type.h"
#include "ble_config.h"
#include "sl_common_log.h"
#include "sl_nvm3_cfg.h"

#include "sl_wlan_app.h"
#include "sl_ble_app.h"

// BLE attribute service types uuid values
#define SL_BLE_SERV_CHAR_UUID   0x2803
#define SL_BLE_CLIENT_CHAR_UUID 0x2902

// BLE characteristic service uuid
#define SL_BLE_CHAT_APP_SERVICE_UUID 0xAABB
#define SL_BLE_CHAT_ATTRIBUTE_1_UUID 0x1AA1
#define SL_BLE_CHAT_ATTRIBUTE_2_UUID 0x1BB1
#define SL_BLE_CHAT_ATTRIBUTE_3_UUID 0x1CC1
// BLE characteristic service handle
#define SL_BLE_CHAT_APP_SERV_HANDLE         10
#define SL_BLE_CHAT_ATTRIBUTE_1_SERV_HANDLE 12
#define SL_BLE_CHAT_ATTRIBUTE_1_VAL_HANDLE  13
#define SL_BLE_CHAT_ATTRIBUTE_2_SERV_HANDLE 14
#define SL_BLE_CHAT_ATTRIBUTE_2_VAL_HANDLE  15
#define SL_BLE_CHAT_ATTRIBUTE_3_SERV_HANDLE 17
#define SL_BLE_CHAT_ATTRIBUTE_3_VAL_HANDLE  18

// local device name
#define SL_BLE_APP_DEVICE_NAME "BLE_CONFIGURATOR"

// attribute properties
#define SL_BLE_ATT_PROPERTY_READ   0x02
#define SL_BLE_ATT_PROPERTY_WRITE  0x08
#define SL_BLE_ATT_PROPERTY_NOTIFY 0x10

#define SL_BLE_CHARACRISTIC_DATA_LEN 6

#define SL_BLE_APP_CHAR_NUMS 3
#define SL_BLE_APP_CHAR_ID1  0
#define SL_BLE_APP_CHAR_ID2  1
#define SL_BLE_APP_CHAR_ID3  2

// application event list
#define SL_BLE_ENH_CONN_EVENT                0x01
#define SL_BLE_DISCONN_EVENT                 0x02
#define SL_BLE_WLAN_SCAN_RESP_EVENT          0x03
#define SL_BLE_CONN_EVENT                    0x04
#define SL_SSID_SENDING_EVENT                0x0D
#define SL_SECTYPE_SENDING_EVENT             0x0E
#define SL_BLE_WLAN_DISCONN_NOTIFY_EVENT     0x0F
#define SL_WLAN_ALREADY_EVENT                0x10
#define SL_WLAN_NOT_ALREADY_EVENT            0x11
#define SL_BLE_WLAN_TIMEOUT_NOTIFY_EVENT     0x12
#define SL_GET_FW_VERSION_EVENT              0x13
#define SL_BLE_WLAN_DISCONNECT_STATUS_EVENT  0x14
#define SL_BLE_WLAN_JOIN_STATUS_EVENT        0x15
#define SL_BLE_MTU_EVENT                     0x16
#define SL_BLE_CONN_UPDATE_EVENT             0x17
#define SL_BLE_RECEIVE_REMOTE_FEATURES_EVENT 0x18
#define SL_BLE_DATA_LENGTH_CHANGE_EVENT      0x19

// Maximum length of SSID
#define SL_SSID_LEN 34
// MAC address length
#define SL_MAC_ADDR_LEN 6
// Maximum Access points that can be scanned
#define SL_AP_SCANNED_MAX 11

#define SL_BLE_CHAT_APP_GATT_ID 0

/*******************************************************************************
 *                DATA TYPES
 ******************************************************************************/

typedef struct {
  uuid_t char_id;
  uint16_t serv_handle;
  uint16_t val_handle;
  uint16_t type;
} char_gatt_t;

typedef struct {
  // service
  uuid_t service_id;
  uint16_t service_handle;
  // attributes
  char_gatt_t chars[SL_BLE_APP_CHAR_NUMS];
} sl_ble_profile_t;

const sl_ble_profile_t sl_ble_app_profile = {
  .service_id     = { .size = 2, .val.val16 = SL_BLE_CHAT_APP_SERVICE_UUID },
  .service_handle = SL_BLE_CHAT_APP_SERV_HANDLE,
  .chars          = {
    { .char_id     = { .size = 2, .val.val16 = SL_BLE_CHAT_ATTRIBUTE_1_UUID },
      .serv_handle = SL_BLE_CHAT_ATTRIBUTE_1_SERV_HANDLE,
      .val_handle  = SL_BLE_CHAT_ATTRIBUTE_1_VAL_HANDLE,
      .type        = SL_BLE_ATT_PROPERTY_WRITE },
    { .char_id     = { .size = 2, .val.val16 = SL_BLE_CHAT_ATTRIBUTE_2_UUID },
      .serv_handle = SL_BLE_CHAT_ATTRIBUTE_2_SERV_HANDLE,
      .val_handle  = SL_BLE_CHAT_ATTRIBUTE_2_VAL_HANDLE,
      .type        = SL_BLE_ATT_PROPERTY_READ | SL_BLE_ATT_PROPERTY_WRITE },
    { .char_id     = { .size = 2, .val.val16 = SL_BLE_CHAT_ATTRIBUTE_3_UUID },
      .serv_handle = SL_BLE_CHAT_ATTRIBUTE_3_SERV_HANDLE,
      .val_handle  = SL_BLE_CHAT_ATTRIBUTE_3_VAL_HANDLE,
      .type        = SL_BLE_ATT_PROPERTY_READ | SL_BLE_ATT_PROPERTY_NOTIFY },
  }
};

const osThreadAttr_t sl_ble_thread_attributes = {
  .name       = "ble_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 2048,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

static osMessageQueueId_t sli_ble_queue;

static rsi_ble_event_conn_status_t sl_conn_app_status;
static rsi_ble_event_disconnect_t sl_disconn_app_status;
sl_ap_t sl_wlan_ap;

static uint8_t sl_ble_remote_dev_addr[18] = { 0 };
static rsi_ble_event_mtu_t app_ble_mtu_event;

/******************************************************
*               Function Declarations
******************************************************/
void sli_ble_on_enhance_conn_status_event(
  rsi_ble_event_enhance_conn_status_t *resp_enh_conn);
void sl_ble_configurator_init(void);
void sl_ble_configurator_task(void *argument);

/*==============================================*/
/**
 * @fn         sli_ble_add_char_serv_att
 * @brief      this function is used to add characteristic service attribute
 * @param[in]  serv_handler, service handler.
 * @param[in]  handle, characteristic service attribute handle.
 * @param[in]  val_prop, characteristic value property.
 * @param[in]  att_val_handle, characteristic value handle
 * @param[in]  att_val_uuid, characteristic value uuid
 * @return     none.
 * @section description
 * This function is used at application to add characteristic attribute
 */
static void sli_ble_add_char_serv_att(void *serv_handler,
                                      uint16_t handle,
                                      uint8_t val_prop,
                                      uint16_t att_val_handle,
                                      uuid_t att_val_uuid)
{
  rsi_ble_req_add_att_t sl_new_att = { 0 };

  // preparing the attribute service structure
  sl_new_att.serv_handler       = serv_handler;
  sl_new_att.handle             = handle;
  sl_new_att.att_uuid.size      = 2;
  sl_new_att.att_uuid.val.val16 = SL_BLE_SERV_CHAR_UUID;
  sl_new_att.property           = SL_BLE_ATT_PROPERTY_READ;

  // preparing the characteristic attribute value
  sl_new_att.data_len = SL_BLE_CHARACRISTIC_DATA_LEN;
  sl_new_att.data[0]  = val_prop;
  rsi_uint16_to_2bytes(&sl_new_att.data[2], att_val_handle);
  rsi_uint16_to_2bytes(&sl_new_att.data[4], att_val_uuid.val.val16);

  // add attribute to the service
  rsi_ble_add_attribute(&sl_new_att);
}

/*==============================================*/
/**
 * @fn         sli_ble_add_char_val_att
 * @brief      this function is used to add characteristic value attribute.
 * @param[in]  serv_handler, new service handler.
 * @param[in]  handle, characteristic value attribute handle.
 * @param[in]  att_type_uuid, attribute uuid value.
 * @param[in]  val_prop, characteristic value property.
 * @param[in]  data, characteristic value data pointer.
 * @param[in]  data_len, characteristic value length.
 * @return     none.
 * @section description
 * This function is used at application to create new service.
 */
static void sli_ble_add_char_val_att(void *serv_handler,
                                     uint16_t handle,
                                     uuid_t att_type_uuid,
                                     uint8_t val_prop,
                                     uint8_t *data,
                                     uint8_t data_len)
{
  rsi_ble_req_add_att_t sl_new_att = { 0 };

  // preparing the attributes
  sl_new_att.serv_handler = serv_handler;
  sl_new_att.handle       = handle;
  memcpy(&sl_new_att.att_uuid, &att_type_uuid, sizeof(uuid_t));
  sl_new_att.property = val_prop;

  // preparing the attribute value
  sl_new_att.data_len = SL_MIN(sizeof(sl_new_att.data), data_len);
  memcpy(sl_new_att.data, data, sl_new_att.data_len);

  // add attribute to the service
  rsi_ble_add_attribute(&sl_new_att);

  // check the attribute property with notification
  if (val_prop & SL_BLE_ATT_PROPERTY_NOTIFY) {
    // if notification property supports then we need to add client characteristic service.

    // preparing the client characteristic attribute & values
    memset(&sl_new_att, 0, sizeof(rsi_ble_req_add_att_t));
    sl_new_att.serv_handler       = serv_handler;
    sl_new_att.handle             = handle + 1;
    sl_new_att.att_uuid.size      = 2;
    sl_new_att.att_uuid.val.val16 = SL_BLE_CLIENT_CHAR_UUID;
    sl_new_att.property = SL_BLE_ATT_PROPERTY_READ | SL_BLE_ATT_PROPERTY_WRITE;
    sl_new_att.data_len = 2;

    // add attribute to the service
    rsi_ble_add_attribute(&sl_new_att);
  }
}

/*==============================================*/
/**
 * @fn         sli_ble_add_configurator_serv
 * @brief      this function is used to add new service i.e., simple chat service.
 * @param[in]  none.
 * @return     int32_t
 *             0  =  success
 *             !0 = failure
 * @section description
 * This function is used at application to create new service.
 */
static uint32_t sli_ble_add_configurator_serv(void)
{
  rsi_ble_resp_add_serv_t sl_new_serv_resp = { 0 };
  uint8_t data[RSI_BLE_MAX_DATA_LEN]       = { 0 };

  rsi_ble_add_service(sl_ble_app_profile.service_id, &sl_new_serv_resp);

  for (int i = 0; i < SL_BLE_APP_CHAR_NUMS; i++) {
    // adding characteristic service attribute to the service
    sli_ble_add_char_serv_att(sl_new_serv_resp.serv_handler,
                              sl_ble_app_profile.chars[i].serv_handle,
                              sl_ble_app_profile.chars[i].type,
                              sl_ble_app_profile.chars[i].val_handle,
                              sl_ble_app_profile.chars[i].char_id);
    // adding characteristic value attribute to the service
    sli_ble_add_char_val_att(sl_new_serv_resp.serv_handler,
                             sl_ble_app_profile.chars[i].val_handle,
                             sl_ble_app_profile.chars[i].char_id,
                             sl_ble_app_profile.chars[i].type,
                             data,
                             RSI_BLE_MAX_DATA_LEN);
  }

  return 0;
}

/*==============================================*/
/**
 * @fn         sli_ble_on_enhance_conn_status_event
 * @brief      invoked when enhanced connection complete event is received
 * @param[out] resp_enh_conn, connected remote device information
 * @return     none.
 * @section description
 * This callback function indicates the status of the connection
 */
void sli_ble_on_enhance_conn_status_event(
  rsi_ble_event_enhance_conn_status_t *resp_enh_conn)
{
  sl_conn_app_status.dev_addr_type = resp_enh_conn->dev_addr_type;
  memcpy(sl_conn_app_status.dev_addr,
         resp_enh_conn->dev_addr,
         RSI_DEV_ADDR_LEN);

  sl_conn_app_status.status = resp_enh_conn->status;
  sli_ble_app_set_event(SL_BLE_ENH_CONN_EVENT,
                        resp_enh_conn,
                        sizeof(rsi_ble_event_enhance_conn_status_t));
}

/*==============================================*/
/**
 * @fn         sli_ble_on_connect_event
 * @brief      invoked when connection complete event is received
 * @param[out] resp_conn, connected remote device information
 * @return     none.
 * @section description
 * This callback function indicates the status of the connection
 */
static void sli_ble_on_connect_event(rsi_ble_event_conn_status_t *resp_conn)
{
  memcpy(&sl_conn_app_status, resp_conn, sizeof(rsi_ble_event_conn_status_t));
  sli_ble_app_set_event(SL_BLE_ENH_CONN_EVENT,
                        resp_conn,
                        sizeof(rsi_ble_event_conn_status_t));
}

/*==============================================*/
/**
 * @fn         sli_ble_on_disconnect_event
 * @brief      invoked when disconnection event is received
 * @param[out]  resp_disconnect, disconnected remote device information
 * @param[out]  reason, reason for disconnection.
 * @return     none.
 * @section description
 * This Callback function indicates disconnected device information and status
 */
static void
sli_ble_on_disconnect_event(rsi_ble_event_disconnect_t *resp_disconnect,
                            uint16_t reason)
{
  UNUSED_PARAMETER(reason);
  memcpy(&sl_disconn_app_status,
         resp_disconnect,
         sizeof(rsi_ble_event_disconnect_t));
  sli_ble_app_set_event(SL_BLE_DISCONN_EVENT,
                        resp_disconnect,
                        sizeof(rsi_ble_event_disconnect_t));
}

/*==============================================*/
/**
 * @fn         sli_ble_on_conn_update_complete_event
 * @brief      invoked when conn update complete event is received
 * @param[out] rsi_ble_event_conn_update_complete contains the controller
 * support conn information.
 * @param[out] resp_status contains the response status (Success or Error code)
 * @return     none.
 * @section description
 * This Callback function indicates the conn update complete event is received
 */
void sli_ble_on_conn_update_complete_event(
  rsi_ble_event_conn_update_t *rsi_ble_event_conn_update_complete,
  uint16_t resp_status)
{
  UNUSED_PARAMETER(resp_status);
  rsi_6byte_dev_address_to_ascii(
    sl_ble_remote_dev_addr,
    (uint8_t *) rsi_ble_event_conn_update_complete->dev_addr);
  sli_ble_app_set_event(SL_BLE_CONN_UPDATE_EVENT,
                        rsi_ble_event_conn_update_complete,
                        sizeof(rsi_ble_event_conn_update_t));
}

/*============================================================================*/
/**
 * @fn         rsi_ble_on_remote_features_event
 * @brief      invoked when LE remote features event is received.
 * @param[out] rsi_ble_event_remote_features, connected remote device information
 * @return     none.
 * @section description
 * This callback function indicates the remote device features
 */
void rsi_ble_on_remote_features_event(
  rsi_ble_event_remote_features_t *rsi_ble_event_remote_features)
{
  sli_ble_app_set_event(SL_BLE_RECEIVE_REMOTE_FEATURES_EVENT,
                        rsi_ble_event_remote_features,
                        sizeof(rsi_ble_event_remote_features_t));
}

/*============================================================================*/
/**
 * @fn         rsi_ble_data_length_change_event
 * @brief      invoked when data length is set
 * @param[out] rsi_ble_data_length_update, data length information
 * @section description
 * This Callback function indicates data length is set
 */
void rsi_ble_data_length_change_event(
  rsi_ble_event_data_length_update_t *rsi_ble_data_length_update)
{
  sli_ble_app_set_event(SL_BLE_DATA_LENGTH_CHANGE_EVENT,
                        rsi_ble_data_length_update,
                        sizeof(rsi_ble_event_data_length_update_t));
}
/*==============================================*/
/**
 * @fn         rsi_ble_on_mtu_event
 * @brief      invoked  when an MTU size event is received
 * @param[out]  rsi_ble_mtu, it indicates MTU size.
 * @return     none.
 * @section description
 * This callback function is invoked  when an MTU size event is received
 */
static void rsi_ble_on_mtu_event(rsi_ble_event_mtu_t *rsi_ble_mtu)
{
  memcpy(&app_ble_mtu_event, rsi_ble_mtu, sizeof(rsi_ble_event_mtu_t));
  rsi_6byte_dev_address_to_ascii(sl_ble_remote_dev_addr,
                                 app_ble_mtu_event.dev_addr);
  sli_ble_app_set_event(SL_BLE_MTU_EVENT,
                        rsi_ble_mtu,
                        sizeof(rsi_ble_event_mtu_t));
}

/*==============================================*/
/**
 * @fn         sli_ble_on_gatt_write_event
 * @brief      this is call back function, it invokes when write/notify events received.
 * @param[out]  event_id, it indicates write/notification event id.
 * @param[out]  rsi_ble_write, write event parameters.
 * @return     none.
 * @section description
 * This is a callback function
 */
static void sli_ble_on_gatt_write_event(uint16_t event_id,
                                        rsi_ble_event_write_t *rsi_ble_write)
{
  UNUSED_PARAMETER(event_id);
  uint8_t cmdid;

  //  Requests will come from Mobile app
  LOG_PRINTF("handle from ble: l:%u, r:%u\n",
             sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID1].val_handle,
             *((uint16_t *) rsi_ble_write->handle));

  if ((sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID1].val_handle)
      == *((uint16_t *) rsi_ble_write->handle)) {
    cmdid = rsi_ble_write->att_value[0];

    switch (cmdid) {
      // Scan command request
      case '3':
      {
        LOG_PRINTF("Received scan request\n");
        uint8_t data = 0;
        sl_wlan_app_set_event(WIFI_APP_SCAN_STATE, &data, 1);
      } break;

      // Sending SSID
      case '2':
      {
        strncpy((char *) sl_wlan_ap.ssid,
                (const char *) &rsi_ble_write->att_value[3],
                sizeof(sl_wlan_ap.ssid) - 1);
        sl_wlan_ap.ssid[sizeof(sl_wlan_ap.ssid) - 1] = '\0';
#if (SL_WLAN_MOCK_ENABLE)
        sl_wlan_app_set_event(WIFI_APP_JOIN_STATE,
                              (uint8_t *) &sl_wlan_ap,
                              sizeof(sl_wlan_ap));
#else
        sli_ble_app_set_event(SL_SSID_SENDING_EVENT,
                              sl_wlan_ap.ssid,
                              sizeof(sl_wlan_ap.ssid));
#endif
      } break;

      // Sending Security type
      case '5':
      {
#if (!SL_WLAN_MOCK_ENABLE)
        if (sl_wlan_connect_status() != SL_WLAN_STATUS_CONNECTED) {
          char sec_type = ((rsi_ble_write->att_value[3]) - '0');
          LOG_PRINTF("In Security Request\n");

          sli_ble_app_set_event(SL_SECTYPE_SENDING_EVENT, &sec_type, 1);
        }
#endif
      } break;

      // Sending PSK
      case '6':
      {
#if (!SL_WLAN_MOCK_ENABLE)
        if (sl_wlan_connect_status() != SL_WLAN_STATUS_CONNECTED) {
          strncpy((char *) sl_wlan_ap.pwd,
                  (const char *) &rsi_ble_write->att_value[3],
                  sizeof(sl_wlan_ap.pwd) - 1);
          sl_wlan_ap.pwd[sizeof(sl_wlan_ap.pwd) - 1] = '\0';
          LOG_PRINTF("PWD from ble app\n");
          sl_wlan_app_set_event(WIFI_APP_JOIN_STATE,
                                (uint8_t *) &sl_wlan_ap,
                                sizeof(sl_wlan_ap));
        }
#endif
      } break;

      // WLAN Status Request
      case '7':
      {
        LOG_PRINTF("WLAN status request received\n");
        if (sl_wlan_connect_status() == SL_WLAN_STATUS_CONNECTED) {
          sli_ble_app_set_event(SL_WLAN_ALREADY_EVENT, NULL, 0);
        } else {
          sli_ble_app_set_event(SL_WLAN_NOT_ALREADY_EVENT, NULL, 0);
        }
      } break;

      // WLAN disconnect request
      case '4':
      {
        LOG_PRINTF("WLAN disconnect request received\n");
        sl_wlan_app_set_event(WIFI_APP_DISCONN_NOTIFY_STATE, NULL, 0);
      } break;

      // FW version request
      case '8':
      {
        sli_ble_app_set_event(SL_GET_FW_VERSION_EVENT, NULL, 0);
        LOG_PRINTF("FW version request\n");
      } break;

      default:
        LOG_PRINTF("Default command case \n\n");
        break;
    }
  }
}

/*==============================================*/
/**
 * @fn         rsi_ble_app_init
 * @brief      initialize the BLE module.
 * @param[in]  none
 * @return     none.
 * @section description
 * This function is used to initialize the BLE module
 */
void sl_ble_configurator_init(void)
{
  uint8_t adv[31] = { 2, 1, 6 };

  sli_ble_add_configurator_serv(); // adding simple BLE chat service

  // registering the GAP callback functions
  rsi_ble_gap_register_callbacks(NULL,
                                 sli_ble_on_connect_event,
                                 sli_ble_on_disconnect_event,
                                 NULL,
                                 NULL,
                                 rsi_ble_data_length_change_event,
                                 sli_ble_on_enhance_conn_status_event,
                                 NULL,
                                 sli_ble_on_conn_update_complete_event,
                                 NULL);
  //! registering the GAP extended call back functions
  rsi_ble_gap_extended_register_callbacks(rsi_ble_on_remote_features_event,
                                          NULL);

  // registering the GATT callback functions
  rsi_ble_gatt_register_callbacks(NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  sli_ble_on_gatt_write_event,
                                  NULL,
                                  NULL,
                                  NULL,
                                  rsi_ble_on_mtu_event,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL,
                                  NULL);

  // Set local name
  rsi_bt_set_local_name((uint8_t *) SL_BLE_APP_DEVICE_NAME);

  // prepare advertise data //local/device name
  adv[3] = strlen(SL_BLE_APP_DEVICE_NAME) + 1;
  adv[4] = 9;
  strncpy((char *) &adv[5], SL_BLE_APP_DEVICE_NAME, sizeof(adv) - 5 - 1);
  adv[sizeof(adv) - 1] = '\0';

  // set advertise data
  rsi_ble_set_advertise_data(adv, strlen(SL_BLE_APP_DEVICE_NAME) + 5);

  // set device in advertising mode.
  rsi_ble_start_advertising();
  LOG_PRINTF("\r\nBLE Advertising Started...\r\n");
}

/*==============================================*/
/**
 * @fn         sli_ble_event_connect_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_connect_handler(sl_ble_event_t *evt)
{
  sl_status_t status;
  // event invokes when connection was completed
  LOG_PRINTF("sli_ble_event_connect_handler: %ld\n", evt->event_id);

  rsi_ble_event_conn_status_t *conn =
    (rsi_ble_event_conn_status_t *) evt->event_data;

  //MTU exchange
  status = rsi_ble_mtu_exchange_event(conn->dev_addr, BLE_MTU_SIZE);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\n MTU request failed with error code %lx", status);
  }
  status = rsi_ble_conn_params_update(conn->dev_addr,
                                      CONN_INTERVAL_DEFAULT_MIN,
                                      CONN_INTERVAL_DEFAULT_MAX,
                                      CONNECTION_LATENCY,
                                      SUPERVISION_TIMEOUT);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\n rsi_ble_conn_params_update command failed : %lx", status);
  }
}

/*==============================================*/
/**
 * @fn         sli_ble_event_disconnect_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_disconnect_handler(sl_ble_event_t *evt)
{
  sl_status_t status;
  uint8_t remote_dev_addr[18]      = { 0 };
  rsi_ble_event_disconnect_t *conn = (rsi_ble_event_disconnect_t *) evt;
  // event invokes when disconnection was completed

  LOG_PRINTF("\r\nDisconnected - sl_ble_remote_dev_addr : %s\r\n",
             rsi_6byte_dev_address_to_ascii(remote_dev_addr, conn->dev_addr));

  // set device in advertising mode.

  do {
    status = rsi_ble_start_advertising();
  } while (status != SL_STATUS_OK);

  LOG_PRINTF("\r\nStarted Advertising \n");
}

/*==============================================*/
/**
 * @fn         sli_ble_event_get_ta_version_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_get_ta_version_handler(sl_ble_event_t *evt)
{
  sl_status_t status;
  sl_wifi_firmware_version_t firmware_version = { 0 };
  uint8_t respond[RSI_BLE_MAX_DATA_LEN]       = { 0 };

  UNUSED_PARAMETER(evt);

  status = sl_wifi_get_firmware_version(&firmware_version);
  if (status == SL_STATUS_OK) {
    respond[0] = 0x08;
    respond[1] = sizeof(sl_wifi_firmware_version_t);
    memcpy(&respond[2], &firmware_version, sizeof(sl_wifi_firmware_version_t));

    rsi_ble_set_local_att_value(
      sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
      RSI_BLE_MAX_DATA_LEN,
      respond);

    print_firmware_version(&firmware_version);
  }
}

/*==============================================*/
/**
 * @fn         sli_ble_event_report_wlan_connected_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_report_wlan_connected_handler(sl_ble_event_t *evt)
{
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };

  UNUSED_PARAMETER(evt);

  respond[1] =
    SL_WLAN_STATUS_CONNECTED;   /*This index will indicate wlan AP connect or disconnect status to Android app*/
  respond[0] = 0x07;

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);
}

/*==============================================*/
/**
 * @fn         sli_ble_event_report_wlan_not_already_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_report_wlan_not_already_handler(sl_ble_event_t *evt)
{
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };

  UNUSED_PARAMETER(evt);

  respond[0] = 0x07;
  respond[1] = 0x00;

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);
}

/*==============================================*/
/**
 * @fn         sli_ble_event_notify_wlan_disconnected_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_notify_wlan_disconnected_handler(sl_ble_event_t *evt)
{
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };

  UNUSED_PARAMETER(evt);

  respond[1] = 0x01;
  respond[0] = 0x04;

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);
}

/*==============================================*/
/**
 * @fn         sli_ble_event_report_wlan_timeout_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_report_wlan_timeout_handler(sl_ble_event_t *evt)
{
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };

  UNUSED_PARAMETER(evt);

  respond[0] = 0x02;
  respond[1] = 0x00;

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);
}

/*==============================================*/
/**
 * @fn         sli_ble_event_report_wlan_status_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_report_wlan_status_handler(sl_ble_event_t *evt)
{
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };
  UNUSED_PARAMETER(evt);

  respond[0] = 0x01;

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);
}

/*==============================================*/
/**
 * @fn         sli_ble_event_sectype_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_sectype_handler(sl_ble_event_t *evt)
{
  uint8_t *security   = (uint8_t *) evt->event_data;
  sl_wlan_ap.sec_type = security[0];

  if (security[0] == 0) {
#if (!SL_WLAN_MOCK_ENABLE)
    // make ap info
    sl_wlan_app_set_event(WIFI_APP_JOIN_STATE,
                          (uint8_t *) &sl_wlan_ap,
                          sizeof(sl_wlan_ap));
#endif
  }
}

/*==============================================*/
/**
 * @fn         sli_ble_event_resp_scan_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_resp_scan_handler(sl_ble_event_t *evt)
{
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };
  sl_wifi_scan_result_t *ap_list = (sl_wifi_scan_result_t *) evt->event_data;
  uint32_t length                = 0;

  respond[0] = 0x03;
  respond[1] = ap_list->scan_count;

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);

  for (uint32_t scan_ix = 0; scan_ix < ap_list->scan_count; scan_ix++) {
    memset(respond, 0, RSI_BLE_MAX_DATA_LEN);
    respond[0] = ap_list->scan_info[scan_ix].security_mode;
    respond[1] = ',';
    strncpy((char *) respond + 2,
            (const char *) ap_list->scan_info[scan_ix].ssid,
            RSI_BLE_MAX_DATA_LEN - 2 - 1);
    respond[RSI_BLE_MAX_DATA_LEN - 1] = '\0';

    length                            = strlen((char *) respond + 2);
    length                            = length + 2;

    rsi_ble_set_local_att_value(
      sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID3].val_handle,
      RSI_BLE_MAX_DATA_LEN,
      respond);

    osDelay(10);
  }

  LOG_PRINTF("Displayed scan list in Silabs app\n\n");
}

/*==============================================*/
/**
 * @fn         sli_ble_event_wlan_join_status_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_wlan_join_status_handler(sl_ble_event_t *evt)
{
  sl_status_t status;
  uint8_t respond[RSI_BLE_MAX_DATA_LEN] = { 0 };
  sl_mac_address_t mac_addr             = { 0 };
  sl_ip_address_t ip                    = { 0 };
  int32_t i, j;

  sl_wlan_inst_t *wlan_p = (sl_wlan_inst_t *) evt->event_data;
  if (wlan_p == NULL) {
    LOG_PRINTF("join event error\n\n");
    sli_ble_app_set_event(SL_BLE_WLAN_TIMEOUT_NOTIFY_EVENT, NULL, 0);
    return;
  }
  sl_net_ip_configuration_t ip_address = wlan_p->ip_address;

  ip.type        = ip_address.type;
  ip.ip.v4.value = ip_address.ip.v4.ip_address.value;

  respond[0] = 0x02;
  respond[1] = 0x01;
  respond[2] = ',';

  // Copy the MAC address
  status = sl_wifi_get_mac_address(SL_WIFI_CLIENT_INTERFACE, &mac_addr);
  if (status == SL_STATUS_OK) {
    for (j = 0; j < 6; j++) {
      respond[j + 3] = mac_addr.octet[j];
    }
  } else {
    j = 6;
  }
  respond[j + 3] = ',';

  // IP Address
  for (i = 0; j < 10; j++, i++) {
    respond[j + 4] = ip.ip.v4.bytes[i];
  }

  rsi_ble_set_local_att_value(
    sl_ble_app_profile.chars[SL_BLE_APP_CHAR_ID2].val_handle,
    RSI_BLE_MAX_DATA_LEN,
    respond);
  LOG_PRINTF("AP joined successfully\n\n");
}

/*==============================================*/
/**
 * @fn         sli_ble_event_remote_features_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sli_ble_event_remote_features_handler(sl_ble_event_t *evt)
{
  sl_status_t status;
  rsi_ble_event_remote_features_t rmf =
    *((rsi_ble_event_remote_features_t *) evt->event_data);

  if (rmf.remote_features[0] & 0x20) {
    status = rsi_ble_set_data_len(sl_conn_app_status.dev_addr, TX_LEN, TX_TIME);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("\n set data length cmd failed with error code = "
                 "%lx \n",
                 status);
    }
  }
}

const sl_ble_event_manage_t sl_ble_event_table[] = {
  { .event_id = SL_BLE_ENH_CONN_EVENT, .cb = sli_ble_event_connect_handler },
  { .event_id = SL_BLE_DISCONN_EVENT, .cb = sli_ble_event_disconnect_handler },
  { .event_id = SL_GET_FW_VERSION_EVENT,
    .cb       = sli_ble_event_get_ta_version_handler },
  { .event_id = SL_WLAN_ALREADY_EVENT,
    .cb       = sli_ble_event_report_wlan_connected_handler },
  { .event_id = SL_WLAN_NOT_ALREADY_EVENT,
    .cb       = sli_ble_event_report_wlan_not_already_handler },
  { .event_id = SL_BLE_WLAN_DISCONN_NOTIFY_EVENT,
    .cb       = sli_ble_event_notify_wlan_disconnected_handler },
  { .event_id = SL_BLE_WLAN_TIMEOUT_NOTIFY_EVENT,
    .cb       = sli_ble_event_report_wlan_timeout_handler },
  { .event_id = SL_BLE_WLAN_DISCONNECT_STATUS_EVENT,
    .cb       = sli_ble_event_report_wlan_status_handler },
  { .event_id = SL_SECTYPE_SENDING_EVENT, .cb = sli_ble_event_sectype_handler },
  { .event_id = SL_BLE_WLAN_SCAN_RESP_EVENT,
    .cb       = sli_ble_event_resp_scan_handler },
  { .event_id = SL_BLE_WLAN_JOIN_STATUS_EVENT,
    .cb       = sli_ble_event_wlan_join_status_handler },
  { .event_id = SL_BLE_RECEIVE_REMOTE_FEATURES_EVENT,
    .cb       = sli_ble_event_remote_features_handler },
};

#define SL_EVENT_TABLE_SIZE \
  sizeof(sl_ble_event_table) / sizeof(sl_ble_event_manage_t)

#define SLI_BLE_EVENT_SYSTEM_QUEUE_SIZE 10

/*==============================================*/
/**
 * @fn         sli_ble_app_set_event
 * @brief      sets the specific event.
 * @param[in]  event_num, specific event number.
 * @return     none.
 * @section description
 * This function is used to set/raise the specific event.
 */
sl_status_t
sli_ble_app_set_event(uint32_t evt_id, void *evt_data, uint32_t data_len)
{
  void *q_event_data = NULL;
  if (evt_data) {
    q_event_data = pvPortMalloc(data_len);
    if (!q_event_data) {
      return SL_STATUS_ALLOCATION_FAILED;
    }
    memcpy(q_event_data, evt_data, data_len);
  }

  sl_ble_event_t msg = { .event_id          = evt_id,
                         .event_data        = q_event_data,
                         .event_data_length = data_len };

  osMessageQueuePut(sli_ble_queue, (void *) &msg, 0, osWaitForever);
  return SL_STATUS_OK;
}

static void sli_ble_handle_event(sl_ble_event_t *recv_event)
{
  for (uint32_t i = 0; i < SL_EVENT_TABLE_SIZE; i++) {
    if (recv_event->event_id == sl_ble_event_table[i].event_id) {
      if (sl_ble_event_table[i].cb) {
        sl_ble_event_table[i].cb(recv_event);
      }
      if (recv_event->event_data) {
        vPortFree(recv_event->event_data);
      }
      break;
    }
  }
}

/*==============================================*/
/**
 * @fn         rsi_ble_app_task
 * @brief      this function will execute when BLE events are raised.
 * @param[in]  none.
 * @return     none.
 * @section description
 */
void sl_ble_configurator_task(void *argument)
{
  UNUSED_PARAMETER(argument);

  if (sl_store_wifi_check() == SL_STATUS_OK) {
    sl_store_wifi_read(&sl_wlan_ap);
    // post an event to Wlan_app
    sl_wlan_app_set_event(WIFI_APP_JOIN_STATE,
                          (uint8_t *) &sl_wlan_ap,
                          sizeof(sl_wlan_ap));
    LOG_PRINTF("AP existed: ssid - %s\n", sl_wlan_ap.ssid);
  }

  while (1) {
    sl_ble_event_t recv_event;
    // wait until message got.
    if (osMessageQueueGet(sli_ble_queue, &recv_event, NULL, osWaitForever)
        == osOK) {
      LOG_PRINTF("ble evt: %ld\n", recv_event.event_id);
      sli_ble_handle_event(&recv_event);
    }
  }
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_send_to_ble
 * @brief      this function is used to send data to ble app.
 * @param[in]   msg_type, it indicates write/notification event id.
 * @param[in]  data, raw data pointer.
 * @param[in]  data_len, raw data length.
 * @return     none.
 * @section description
 */
void sl_wlan_app_send_to_ble(uint16_t msg_type,
                             uint8_t *data,
                             uint16_t data_len)
{
  switch (msg_type) {
    case WIFI_APP_SCAN_RESP:

      sli_ble_app_set_event(SL_BLE_WLAN_SCAN_RESP_EVENT, data, data_len);
      break;
    case WIFI_APP_CONNECTION_STATUS:
      sli_ble_app_set_event(SL_BLE_WLAN_JOIN_STATUS_EVENT, data, data_len);
      break;
    case WIFI_APP_DISCONNECTION_STATUS:
      sli_ble_app_set_event(SL_BLE_WLAN_DISCONNECT_STATUS_EVENT,
                            data,
                            data_len);
      break;
    case WIFI_APP_DISCONNECTION_NOTIFY:
      sli_ble_app_set_event(SL_BLE_WLAN_DISCONN_NOTIFY_EVENT, data, data_len);
      break;
    case WIFI_APP_TIMEOUT_NOTIFY:
      sli_ble_app_set_event(SL_BLE_WLAN_TIMEOUT_NOTIFY_EVENT, data, data_len);
      break;
    default:
      break;
  }
}

/*==============================================*/
/**
 * @fn         sl_ble_init
 * @brief      this function is used to init to ble app.
 * @return     none.
 * @section description
 */
void sl_ble_init(void)
{
  LOG_PRINTF("ble thread start\n");
  sl_ble_configurator_init();

  sli_ble_queue = osMessageQueueNew(SLI_BLE_EVENT_SYSTEM_QUEUE_SIZE,
                                    sizeof(sl_event_system_msg_t),
                                    NULL);

  if (osThreadNew((osThreadFunc_t) sl_ble_configurator_task,
                  NULL,
                  &sl_ble_thread_attributes) == NULL) {
    LOG_PRINTF("Failed to create BLE thread\n");
  }
}
