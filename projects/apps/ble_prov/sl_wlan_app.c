/*******************************************************************************
 * @file  sl_wlan_app.c
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
#include "ble_config.h"
#include "sl_constants.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"
#include "sl_net.h"
#include "sl_net_si91x.h"
#include "sl_utility.h"

#include "cmsis_os2.h"
#include <string.h>

#include "rsi_common_apis.h"
#include "rsi_bt_common_apis.h"
#include "sl_si91x_m4_ps.h"

#include "sl_common_config.h"
#include "sl_common_type.h"
#include "sl_gw_info.h"
#include "sl_common_log.h"
#include "modules/sl_si917_net.h"

#include "sl_nvm3_cfg.h"

#include "sl_ble_app.h"
#include "sl_wlan_app.h"

#if SL_USE_LWIP_STACK
#include "sl_net_for_lwip.h"
#endif

#ifndef TX_POOL_RATIO
#define TX_POOL_RATIO 1
#endif

#ifndef RX_POOL_RATIO
#define RX_POOL_RATIO 1
#endif

#ifndef GLOBAL_POOL_RATIO
#define GLOBAL_POOL_RATIO 1
#endif

#define SL_DHCP_HOST_NAME "si917.gw"

#if (SL_WLAN_MOCK_ENABLE)
#define SL_MOCK_AP_SSID     "ASUS-E0"
#define SL_MOCK_AP_PWD      "12345678"
#define SL_MOCK_AP_SECURITY SL_WIFI_WPA_WPA2_MIXED
#endif // SL_WLAN_MOCK_ENABLE

// APP version
#define APP_FW_VERSION "0.4"

#define SL_WLAN_CONNECT_TIMEOUT_MS 10000
#define SL_WLAN_SCAN_TIMEOUT       10000
#define SL_WLAN_COUNT_CONNECT_MAX  3

#define SL_WLAN_SCAN_RESULT_LENGTH \
  (sizeof(sl_wifi_scan_result_t)   \
   + (SL_WIFI_MAX_SCANNED_AP       \
      * sizeof(sl_wlan_inst.sl_wlan_scan_result->scan_info[0])))

/*
 *******************************************************************************
 *                      LOCAL GLOBAL VARIABLES
 *******************************************************************************
 */
// WLAN include file for configuration
static const sl_wifi_device_configuration_t wl_cli_config = {
  .boot_option = LOAD_NWP_FW,
  .mac_address = NULL,
  .band        = SL_SI91X_WIFI_BAND_2_4GHZ,
  .region_code = US,
  .boot_config = {
    .oper_mode       = SL_SI91X_CLIENT_MODE,
    .coex_mode       = SL_SI91X_WLAN_BLE_MODE,
    .feature_bit_map = (SL_SI91X_FEAT_ULP_GPIO_BASED_HANDSHAKE
                        | SL_SI91X_FEAT_DEV_TO_HOST_ULP_GPIO_1
#ifdef SLI_SI91X_MCU_INTERFACE
                        | SL_SI91X_FEAT_WPS_DISABLE
#endif
                        ),
    .tcp_ip_feature_bit_map =
      (SL_SI91X_TCP_IP_FEAT_DHCPV4_CLIENT | SL_SI91X_TCP_IP_FEAT_SSL
       | SL_SI91X_TCP_IP_FEAT_EXTENSION_VALID
       | SL_SI91X_TCP_IP_FEAT_DNS_CLIENT | SL_SI91X_TCP_IP_FEAT_BYPASS
       | SL_SI91X_TCP_IP_FEAT_ICMP | SL_SI91X_TCP_IP_FEAT_HTTP_CLIENT),
    .custom_feature_bit_map = (SL_SI91X_CUSTOM_FEAT_EXTENTION_VALID
                               | SL_SI91X_CUSTOM_FEAT_SOC_CLK_CONFIG_160MHZ),
    .ext_custom_feature_bit_map =
      (SL_SI91X_EXT_FEAT_LOW_POWER_MODE | SL_SI91X_EXT_FEAT_XTAL_CLK
       | MEMORY_CONFIG
#ifdef SLI_SI917
       | SL_SI91X_EXT_FEAT_FRONT_END_SWITCH_PINS_ULP_GPIO_4_5_0
#endif // SLI_SI917
       | SL_SI91X_EXT_FEAT_BT_CUSTOM_FEAT_ENABLE),
    .bt_feature_bit_map =
      (SL_SI91X_BT_RF_TYPE | SL_SI91X_ENABLE_BLE_PROTOCOL),
    .ext_tcp_ip_feature_bit_map =
      (SL_SI91X_CONFIG_FEAT_EXTENTION_VALID
       | SL_SI91X_EXT_TCP_IP_WINDOW_DIV
       | SL_SI91X_EXT_TCP_IP_WAIT_FOR_SOCKET_CLOSE
       | SL_SI91X_EXT_FEAT_HTTP_OTAF_SUPPORT),
    //!ENABLE_BLE_PROTOCOL in bt_feature_bit_map
    .ble_feature_bit_map =
      ((SL_SI91X_BLE_MAX_NBR_PERIPHERALS(RSI_BLE_MAX_NBR_PERIPHERALS)
        | SL_SI91X_BLE_MAX_NBR_CENTRALS(RSI_BLE_MAX_NBR_CENTRALS)
        | SL_SI91X_BLE_MAX_NBR_ATT_SERV(RSI_BLE_MAX_NBR_ATT_SERV)
        | SL_SI91X_BLE_MAX_NBR_ATT_REC(RSI_BLE_MAX_NBR_ATT_REC))
       | SL_SI91X_FEAT_BLE_CUSTOM_FEAT_EXTENTION_VALID
       | SL_SI91X_BLE_PWR_INX(RSI_BLE_PWR_INX)
       | SL_SI91X_BLE_PWR_SAVE_OPTIONS(RSI_BLE_PWR_SAVE_OPTIONS)
       | SL_SI91X_916_BLE_COMPATIBLE_FEAT_ENABLE
#if RSI_BLE_GATT_ASYNC_ENABLE
       | SL_SI91X_BLE_GATT_ASYNC_ENABLE
#endif
      ),

    .ble_ext_feature_bit_map =
      ((SL_SI91X_BLE_NUM_CONN_EVENTS(RSI_BLE_NUM_CONN_EVENTS)
        | SL_SI91X_BLE_NUM_REC_BYTES(RSI_BLE_NUM_REC_BYTES))
#if RSI_BLE_INDICATE_CONFIRMATION_FROM_HOST
       |
       SL_SI91X_BLE_INDICATE_CONFIRMATION_FROM_HOST       //indication response from app
#endif
#if RSI_BLE_MTU_EXCHANGE_FROM_HOST
       |
       SL_SI91X_BLE_MTU_EXCHANGE_FROM_HOST       //MTU Exchange request initiation from app
#endif
#if RSI_BLE_SET_SCAN_RESP_DATA_FROM_HOST
       |
       (SL_SI91X_BLE_SET_SCAN_RESP_DATA_FROM_HOST)       //Set SCAN Resp Data from app
#endif
#if RSI_BLE_DISABLE_CODED_PHY_FROM_HOST
       |
       (SL_SI91X_BLE_DISABLE_CODED_PHY_FROM_HOST)       //Disable Coded PHY from app
#endif
#if BLE_SIMPLE_GATT
       | SL_SI91X_BLE_GATT_INIT
#endif
      ),
    .config_feature_bit_map = (SL_SI91X_FEAT_SLEEP_GPIO_SEL_BITMAP)
  }
};

const osThreadAttr_t sl_wlan_thread_attributes = {
  .name       = "wlan_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 4096,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

static osMessageQueueId_t sli_wlan_queue;

static sl_wlan_inst_t sl_wlan_inst = {
  .sl_wlan_scan_completed       = false,
  .sl_wlan_scan_callback_status = SL_STATUS_OK,
  .sl_wlan_connect_status       = SL_WLAN_STATUS_DISCONNECTED,
  .sl_wlan_disassosiated        = 0,
  .sl_count_try_connect         = 0,
};

static sl_net_wifi_lwip_context_t wifi_client_context;

#if (SL_WLAN_MOCK_ENABLE)
void sl_wlan_start_mock(void);
void sl_wlan_scan_mock(void);
#endif // SL_WLAN_MOCK_ENABLE

/*
 *******************************************************************************
 *                         PRIVATE FUNCTIONS
 *******************************************************************************
 */

static sl_status_t sl_wlan_show_scan_results();

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
sl_wlan_app_set_event(uint32_t evt_id, uint8_t *evt_data, uint32_t data_len)
{
  void *q_event_data = NULL;
  if (evt_data) {
    q_event_data = pvPortMalloc(data_len);
    if (q_event_data == NULL) {
      return SL_STATUS_ALLOCATION_FAILED;
    }
    memcpy(q_event_data, evt_data, data_len);
  }

  sl_wlan_event_t msg = { .event_id          = evt_id,
                          .event_data        = q_event_data,
                          .event_data_length = data_len };

  osMessageQueuePut(sli_wlan_queue, (void *) &msg, 0, osWaitForever);

  return SL_STATUS_OK;
}

sl_status_t wlan_app_scan_callback_handler(sl_wifi_event_t event,
                                           sl_wifi_scan_result_t *result,
                                           uint32_t result_length,
                                           void *arg)
{
  UNUSED_PARAMETER(arg);
  UNUSED_PARAMETER(result_length);

  if (SL_WIFI_CHECK_IF_EVENT_FAILED(event)) {
    sl_wlan_inst.sl_wlan_scan_callback_status = *(sl_status_t *) result;
    return SL_STATUS_FAIL;
  }
  SL_VERIFY_POINTER_OR_RETURN(sl_wlan_inst.sl_wlan_scan_result,
                              SL_STATUS_NULL_POINTER);
  memset(sl_wlan_inst.sl_wlan_scan_result, 0, SL_WLAN_SCAN_RESULT_LENGTH);
  memcpy(sl_wlan_inst.sl_wlan_scan_result, result, SL_WLAN_SCAN_RESULT_LENGTH);

  sl_wlan_inst.sl_wlan_scan_callback_status = sl_wlan_show_scan_results();
  sl_wlan_inst.sl_wlan_scan_completed       = true;

  return SL_STATUS_OK;
}

// rejoin failure callback handler in station mode
sl_status_t join_callback_handler(sl_wifi_event_t event,
                                  char *result,
                                  uint32_t result_length,
                                  void *arg)
{
  UNUSED_PARAMETER(event);
  UNUSED_PARAMETER(result);
  UNUSED_PARAMETER(result_length);
  UNUSED_PARAMETER(arg);

  // update wlan application state
  sl_wlan_inst.sl_wlan_connect_status = SL_WLAN_STATUS_DISCONNECTED;

  sl_wlan_app_set_event(WIFI_APP_DISCONNECTED_STATE, NULL, 0);

  return SL_STATUS_OK;
}

/*==============================================*/
/**
 * @fn         sl_wlan_show_scan_results
 * @brief
 * @return     none.
 * @section description
 */
static sl_status_t sl_wlan_show_scan_results()
{
  SL_WIFI_ARGS_CHECK_NULL_POINTER(sl_wlan_inst.sl_wlan_scan_result);
  uint8_t *bssid = NULL;
  LOG_PRINTF("%lu Scan results:\n",
             sl_wlan_inst.sl_wlan_scan_result->scan_count);

  if (sl_wlan_inst.sl_wlan_scan_result->scan_count) {
    LOG_PRINTF("\n   %s %24s %s", "SSID", "SECURITY", "NETWORK");
    LOG_PRINTF("%12s %12s %s\n", "BSSID", "CHANNEL", "RSSI");

    for (int a = 0; a < (int) sl_wlan_inst.sl_wlan_scan_result->scan_count;
         ++a) {
      bssid = (uint8_t *) &sl_wlan_inst.sl_wlan_scan_result->scan_info[a].bssid;
      LOG_PRINTF("%-24s %4u,  %4u, ",
                 sl_wlan_inst.sl_wlan_scan_result->scan_info[a].ssid,
                 sl_wlan_inst.sl_wlan_scan_result->scan_info[a].security_mode,
                 sl_wlan_inst.sl_wlan_scan_result->scan_info[a].network_type);
      LOG_PRINTF("  %02x:%02x:%02x:%02x:%02x:%02x, %4u,  -%u\n",
                 bssid[0],
                 bssid[1],
                 bssid[2],
                 bssid[3],
                 bssid[4],
                 bssid[5],
                 sl_wlan_inst.sl_wlan_scan_result->scan_info[a].rf_channel,
                 sl_wlan_inst.sl_wlan_scan_result->scan_info[a].rssi_val);
    }
  }

  return SL_STATUS_OK;
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_init_state_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sl_wlan_app_init_state_handler(sl_wlan_event_t *evt)
{
  UNUSED_PARAMETER(evt);
  LOG_PRINTF("WIFI App Initial State\n");

  //! Initialize join fail call back
  sl_wifi_set_join_callback(join_callback_handler, NULL);

  // update wlan application state
  sl_wlan_app_set_event(WIFI_APP_SCAN_STATE, NULL, 0);
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_scan_state_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sl_wlan_app_scan_state_handler(sl_wlan_event_t *evt)
{
  UNUSED_PARAMETER(evt);
#if (SL_WLAN_MOCK_ENABLE)
  sl_wlan_scan_mock();
#else
  sl_status_t status;
  sl_wifi_scan_configuration_t wifi_scan_configuration =
    default_wifi_scan_configuration;

  sl_wifi_set_scan_callback(wlan_app_scan_callback_handler, NULL);
  sl_wlan_inst.sl_wlan_scan_completed = false;

  status = sl_wifi_start_scan(SL_WIFI_CLIENT_2_4GHZ_INTERFACE,
                              NULL,
                              &wifi_scan_configuration);
  if (SL_STATUS_IN_PROGRESS == status) {
    LOG_PRINTF("Scanning...\r\n");
    const uint32_t start = osKernelGetTickCount();

    while (!sl_wlan_inst.sl_wlan_scan_completed
           && (osKernelGetTickCount() - start) <= SL_WLAN_SCAN_TIMEOUT) {
      osThreadYield();
    }
    status = sl_wlan_inst.sl_wlan_scan_completed
             ? sl_wlan_inst.sl_wlan_scan_callback_status
             : SL_STATUS_TIMEOUT;
  }
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\r\nWLAN Scan Wait Failed, Error Code : 0x%lX\r\n", status);
    sl_wlan_app_set_event(WIFI_APP_SCAN_STATE, NULL, 0);
    osDelay(1000);
  } else {
    // update wlan application state
    sl_wlan_app_send_to_ble(WIFI_APP_SCAN_RESP,
                            (uint8_t *) sl_wlan_inst.sl_wlan_scan_result,
                            SL_WLAN_SCAN_RESULT_LENGTH);
  }
#endif
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_join_state_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sl_wlan_app_join_state_handler(sl_wlan_event_t *evt)
{
  sl_status_t status;

  sl_wifi_credential_t cred  = { 0 };
  sl_wifi_credential_id_t id = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID;
  sl_ap_t ap                 = *((sl_ap_t *) evt->event_data);

#if (SL_WLAN_MOCK_ENABLE)
  memset(ap.pwd, 0, sizeof(ap.pwd));
  memcpy(ap.pwd, SL_MOCK_AP_PWD, strlen((char *) SL_MOCK_AP_PWD));
  ap.sec_type = SL_MOCK_AP_SECURITY;
#else
  // store ap info.
  sl_status_t ret = sl_store_wifi_write(ap);
  LOG_PRINTF("Store AP status: %ld\n", ret);
#endif // SL_WLAN_MOCK_ENABLE

  sl_wlan_inst.access_point = sl_gw_info.profile.config;

  cred.type = SL_WIFI_PSK_CREDENTIAL;
  memcpy(cred.psk.value, ap.pwd, strlen((char *) ap.pwd));
  status = sl_net_set_credential(id,
                                 SL_NET_WIFI_PSK,
                                 ap.pwd,
                                 strlen((char *) ap.pwd));
  if (SL_STATUS_OK == status) {
    LOG_PRINTF("Credentials set, id : %lu\n", id);

    sl_wlan_inst.access_point.ssid.length = strlen((char *) ap.ssid);
    strncpy((char *) sl_wlan_inst.access_point.ssid.value,
            (const char *) ap.ssid,
            sizeof(sl_wlan_inst.access_point.ssid.value) - 1);
    sl_wlan_inst.access_point.ssid
    .value[sizeof(sl_wlan_inst.access_point.ssid.value) - 1] = '\0';

    sl_wlan_inst.access_point.encryption = SL_WIFI_DEFAULT_ENCRYPTION;
    sl_wlan_inst.access_point.security   = (ap.sec_type == 8) ? 2 : ap.sec_type;
    sl_wlan_inst.access_point.credential_id = id;

    LOG_PRINTF("SSID=%s\n", sl_wlan_inst.access_point.ssid.value);
    LOG_PRINTF("pwd=%s\n", ap.pwd);
    LOG_PRINTF("sec=%d\n", sl_wlan_inst.access_point.security);

    memcpy(&sl_gw_info.profile.config,
           &sl_wlan_inst.access_point,
           sizeof(sl_wlan_inst.access_point));
    LOG_PRINTF("set wifi profile\n");
    sl_net_set_profile(SL_NET_WIFI_CLIENT_INTERFACE,
                       SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID,
                       &sl_gw_info.profile);
    LOG_PRINTF("sl net up\n");
    status = sl_net_up(SL_NET_WIFI_CLIENT_INTERFACE,
                       SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID);
  }

  if (status != SL_STATUS_OK) {
    uint8_t timeout = 1;
    sl_wlan_app_send_to_ble(WIFI_APP_TIMEOUT_NOTIFY, (uint8_t *) &timeout, 1);
    LOG_PRINTF("\r\nWLAN Connect Failed, Error Code : 0x%lX\r\n", status);

    // update wlan application state
    sl_wlan_inst.sl_wlan_connect_status = SL_WLAN_STATUS_DISCONNECTED;
  } else {
    LOG_PRINTF("\n WLAN connection is successful\n");
    // update wlan application state
    sl_wlan_app_set_event(WIFI_APP_CONNECTED_STATE, NULL, 0);
  }

  LOG_PRINTF("WIFI App Join State\n");
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_flash_state_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sl_wlan_app_flash_state_handler(sl_wlan_event_t *evt)
{
  sl_status_t status;
  uint8_t retry = *(uint8_t *) evt->event_data;
  if (retry) {
    status = sl_net_up(SL_NET_WIFI_CLIENT_INTERFACE,
                       SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("\r\nWLAN flash Connect Failed, Error Code : 0x%lX\r\n",
                 status);
    } else {
      sl_wlan_app_set_event(WIFI_APP_CONNECTED_STATE, NULL, 0);
    }
  }
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_connected_state_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sl_wlan_app_connected_state_handler(sl_wlan_event_t *evt)
{
  UNUSED_PARAMETER(evt);

  sl_wlan_inst.ip_address.type      = SL_IPV4;
  sl_wlan_inst.ip_address.mode      = SL_IP_MANAGEMENT_DHCP;
  sl_wlan_inst.ip_address.host_name = SL_DHCP_HOST_NAME;

  LOG_PRINTF("Get IP address\n");

  sl_status_t status;
  // Configure IP
#if SL_USE_LWIP_STACK
  status = sl_net_get_profile(SL_NET_WIFI_CLIENT_INTERFACE,
                              SL_NET_DEFAULT_WIFI_CLIENT_PROFILE_ID,
                              &sl_gw_info.profile);
#else
  status = sl_si91x_configure_ip_address(&sl_wlan_inst.ip_address,
                                         SL_SI91X_WIFI_CLIENT_VAP_ID);
#endif //SL_USE_LWIP_STACK

  if (status != SL_STATUS_OK) {
    sl_wlan_inst.sl_count_try_connect++;
    if (sl_wlan_inst.sl_count_try_connect == SL_WLAN_COUNT_CONNECT_MAX) {
      sl_wlan_inst.sl_count_try_connect = 0;
      uint8_t timeout                   = 1;
      status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
      if (status == SL_STATUS_OK) {
        sl_wlan_inst.sl_wlan_connect_status = SL_WLAN_STATUS_DISCONNECTED;
        sl_wlan_app_send_to_ble(WIFI_APP_TIMEOUT_NOTIFY,
                                (uint8_t *) &timeout,
                                1);
      }
    }
    sl_wlan_app_send_to_ble(WIFI_APP_CONNECTION_STATUS, (uint8_t *) NULL, 0);
    LOG_PRINTF("\r\nIP Config Failed, Error Code : 0x%lX\r\n", status);
  } else {
    sl_wlan_inst.sl_count_try_connect   = 0;
    sl_wlan_inst.sl_wlan_connect_status = SL_WLAN_STATUS_CONNECTED;
    sl_wlan_inst.sl_wlan_disassosiated  = 0;

    memcpy(&sl_wlan_inst.ip_address,
           &sl_gw_info.profile.ip,
           sizeof(sl_gw_info.profile.ip));

#if defined(SL_SI91X_PRINT_DBG_LOG)
    sl_ip_address_t ip = { 0 };
    ip.type            = sl_wlan_inst.ip_address.type;
    ip.ip.v4.value     = sl_wlan_inst.ip_address.ip.v4.ip_address.value;
    print_sl_ip_address(&ip);
#endif

    // update wlan application state
    sl_wlan_app_set_event(WIFI_APP_IPCONFIG_DONE_STATE, NULL, 0);
    sl_wlan_app_send_to_ble(WIFI_APP_CONNECTION_STATUS,
                            (uint8_t *) &sl_wlan_inst,
                            sizeof(sl_wlan_inst));

    memcpy(&sl_gw_info.ip_address,
           &sl_wlan_inst.ip_address.ip.v4.ip_address,
           4);
    memcpy(&sl_gw_info.default_gw, &sl_wlan_inst.ip_address.ip.v4.gateway, 4);
    memcpy(&sl_gw_info.subnet, &sl_wlan_inst.ip_address.ip.v4.netmask, 4);
    sl_s917_net_connect_ap(&sl_gw_info);

    LOG_PRINTF("\nWIFI App Connected State\n");
  }
}

/*==============================================*/
/**
 * @fn         sl_wlan_app_disconnected_state_handler
 * @brief
 * @return     none.
 * @section description
 */
static void sl_wlan_app_disconnected_state_handler(sl_wlan_event_t *evt)
{
  UNUSED_PARAMETER(evt);
  uint8_t disconnected = 1;
  sl_wlan_app_send_to_ble(WIFI_APP_DISCONNECTION_STATUS,
                          (uint8_t *) &disconnected,
                          1);
  sl_wlan_app_set_event(WIFI_APP_FLASH_STATE, NULL, 0);
  LOG_PRINTF("WIFI App Disconnected State\n");
}

static void sl_wlan_app_disconnect_notify_handler(sl_wlan_event_t *evt)
{
  sl_status_t status;
  UNUSED_PARAMETER(evt);

  //  status = sl_wifi_disconnect(SL_WIFI_CLIENT_INTERFACE);
  status = sl_net_down(SL_NET_WIFI_CLIENT_INTERFACE);
  if (status == SL_STATUS_OK) {
#if RSI_WISE_MCU_ENABLE
    rsi_flash_erase((uint32_t) FLASH_ADDR_TO_STORE_AP_DETAILS);
#endif
    LOG_PRINTF("\r\nWLAN Disconnected\r\n");
    LOG_PRINTF("WLAN delete AP stored\n");
    // remove AP stored.
    sl_store_wifi_del();

    LOG_PRINTF("WLAN report status to App\n");
    uint8_t disassosiated               = 1;
    sl_wlan_inst.sl_wlan_connect_status = SL_WLAN_STATUS_DISCONNECTED;

    sl_wlan_app_send_to_ble(WIFI_APP_DISCONNECTION_NOTIFY,
                            (uint8_t *) &disassosiated,
                            1);
    sl_wlan_app_set_event(WIFI_APP_UNCONNECTED_STATE, NULL, 0);
  } else {
    LOG_PRINTF("\r\nWIFI Disconnect Failed, Error Code : 0x%lX\r\n", status);
    uint8_t disassosiated = 0;
    sl_wlan_app_send_to_ble(WIFI_APP_DISCONNECTION_NOTIFY,
                            (uint8_t *) &disassosiated,
                            1);
  }

  LOG_PRINTF("WIFI App Disconnect Notify State\n");
}

const sl_wlan_event_manage_t sl_wlan_event_table[] = {
  { .event_id = WIFI_APP_INITIAL_STATE, .cb = sl_wlan_app_init_state_handler },
  { .event_id = WIFI_APP_SCAN_STATE, .cb = sl_wlan_app_scan_state_handler },
  { .event_id = WIFI_APP_JOIN_STATE, .cb = sl_wlan_app_join_state_handler },
  { .event_id = WIFI_APP_FLASH_STATE, .cb = sl_wlan_app_flash_state_handler },
  { .event_id = WIFI_APP_CONNECTED_STATE,
    .cb       = sl_wlan_app_connected_state_handler },
  { .event_id = WIFI_APP_DISCONNECTED_STATE,
    .cb       = sl_wlan_app_disconnected_state_handler },
  { .event_id = WIFI_APP_DISCONN_NOTIFY_STATE,
    .cb       = sl_wlan_app_disconnect_notify_handler },
  // implement if need
  { .event_id = WIFI_APP_UNCONNECTED_STATE, .cb = NULL },
  { .event_id = WIFI_APP_IPCONFIG_DONE_STATE, .cb = NULL },
  { .event_id = WIFI_APP_ERROR_STATE, .cb = NULL },
};

#define SL_WLAN_ENVENT_MANANGED_NUMBER \
  sizeof(sl_wlan_event_table) / sizeof(sl_wlan_event_manage_t)

static void handle_wlan_event(sl_wlan_event_t *recv_event)
{
  for (uint32_t i = 0; i < SL_WLAN_ENVENT_MANANGED_NUMBER; i++) {
    if (recv_event->event_id == sl_wlan_event_table[i].event_id) {
      if (sl_wlan_event_table[i].cb) {
        sl_wlan_event_table[i].cb(recv_event);
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
 * @fn         wifi_app_task
 * @brief
 * @return     none.
 * @section description
 */
void wifi_app_task(void)
{
  // Allocate memory for scan buffer
  sl_wlan_inst.sl_wlan_scan_result =
    (sl_wifi_scan_result_t *) pvPortMalloc(SL_WLAN_SCAN_RESULT_LENGTH);
  if (sl_wlan_inst.sl_wlan_scan_result == NULL) {
    LOG_PRINTF("Failed to allocate memory for scan result\n");
    return;
  }

#if (SL_WLAN_MOCK_ENABLE)
  sl_wlan_start_mock();
#endif //

  while (1) {
    sl_wlan_event_t recv_event;
    if (osMessageQueueGet(sli_wlan_queue, &recv_event, NULL, osWaitForever)
        == osOK) {
      LOG_PRINTF("wlan evt: %ld\n", recv_event.event_id);

      handle_wlan_event(&recv_event);
    }
  }

  if (sl_wlan_inst.sl_wlan_scan_result != NULL) {
    vPortFree(sl_wlan_inst.sl_wlan_scan_result);
  }
}

/*==============================================*/
/**
 * @fn         sl_wlan_connect_status
 * @brief      get status of wlan instant
 * @return     status, specific status of wlan..
 */
uint32_t sl_wlan_connect_status(void)
{
  return sl_wlan_inst.sl_wlan_connect_status;
}

/*==============================================*/
/**
 * @fn         sl_wlan_init
 * @brief      init wlan
 * @return     none
 */
void sl_wlan_init(void)
{
  sl_status_t status = SL_STATUS_OK;

  //! Wi-Fi initialization
  sl_gw_info.profile = (sl_net_wifi_client_profile_t){
    .config =
    {
      .ssid.value        = "",
      .ssid.length       = 0,
      .channel.channel   = SL_WIFI_AUTO_CHANNEL,
      .channel.band      = SL_WIFI_AUTO_BAND,
      .channel.bandwidth = SL_WIFI_AUTO_BANDWIDTH,
      .channel_bitmap.channel_bitmap_2_4 =
        SL_WIFI_DEFAULT_CHANNEL_BITMAP,
      .bssid          = { { 0 } },
      .bss_type       = SL_WIFI_BSS_TYPE_INFRASTRUCTURE,
      .security       = SL_WIFI_WPA2,
      .encryption     = SL_WIFI_DEFAULT_ENCRYPTION,
      .client_options = 0,
      .credential_id  = SL_NET_DEFAULT_WIFI_CLIENT_CREDENTIAL_ID,
    },
    .ip = {
      .mode      = SL_IP_MANAGEMENT_DHCP,
      .type      = SL_IPV4,
      .host_name = NULL,
      .ip        = { { { 0 } } },
    }
  };

  status = sl_net_init(SL_NET_WIFI_CLIENT_INTERFACE,
                       &wl_cli_config,
                       &wifi_client_context,
                       NULL);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\r\nWi-Fi Initialization Failed, Error Code : 0x%lX\r\n",
               status);
    return;
  }

  LOG_PRINTF("\r\n Wi-Fi initialization is successful\n");

  sl_si917_net_init(&sl_gw_info);
  sl_wifi_set_join_callback(join_callback_handler, NULL);

  sli_wlan_queue = osMessageQueueNew(10, sizeof(sl_wlan_event_t), NULL);

  if (osThreadNew((osThreadFunc_t) wifi_app_task,
                  NULL,
                  &sl_wlan_thread_attributes) == NULL) {
    LOG_PRINTF("Failed to create BLE thread\n");
  }
}

#if (SL_WLAN_MOCK_ENABLE)
void sl_wlan_start_mock(void)
{
  sl_ap_t sl_wlan_ap;

  strncpy((char *) sl_wlan_ap.ssid,
          (const char *) SL_MOCK_AP_SSID,
          sizeof(sl_wlan_ap.ssid) - 1);
  sl_wlan_ap.ssid[sizeof(sl_wlan_ap.ssid) - 1] = '\0';

  sl_wlan_app_set_event(WIFI_APP_JOIN_STATE,
                        (uint8_t *) &sl_wlan_ap,
                        sizeof(sl_wlan_ap));
}

void sl_wlan_scan_mock(void)
{
  sl_wifi_scan_result_t *result =
    malloc(sizeof(sl_wifi_scan_result_t) + sizeof(result->scan_info[0]));
  result->scan_count                 = 1;
  result->scan_info[0].rf_channel    = 6;
  result->scan_info[0].security_mode = SL_WIFI_WPA2;
  result->scan_info[0].rssi_val      = -20;
  result->scan_info[0].network_type  = 1;

  strncpy((char *) result->scan_info[0].ssid,
          SL_MOCK_AP_SSID,
          sizeof(result->scan_info[0].ssid) - 1);
  result->scan_info[0].ssid[sizeof(result->scan_info[0].ssid) - 1] = '\0';
  result->scan_info[0].bssid[0]                                    = 0x04;
  result->scan_info[0].bssid[1]                                    = 0xd9;
  result->scan_info[0].bssid[2]                                    = 0xf5;
  result->scan_info[0].bssid[3]                                    = 0x5c;
  result->scan_info[0].bssid[4]                                    = 0x86;
  result->scan_info[0].bssid[5]                                    = 0xe0;

  sl_wlan_app_send_to_ble(WIFI_APP_SCAN_RESP,
                          (uint8_t *) result,
                          sizeof(sl_wifi_scan_result_t)
                          + sizeof(result->scan_info[0]));
}
#endif // SL_WLAN_MOCK_ENABLE
