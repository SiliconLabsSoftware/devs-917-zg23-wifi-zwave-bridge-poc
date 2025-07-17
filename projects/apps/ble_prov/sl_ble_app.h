/***************************************************************************/ /**
 * @file sl_ble_app.h
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

#ifndef SL_BLE_APP_H
#define SL_BLE_APP_H

/***************************************************************************/ /**
 * Initialize ble application.
 ******************************************************************************/
typedef void (*sl_ble_event_handle_t)(sl_ble_event_t *evt);
typedef struct {
  uint32_t event_id;
  sl_ble_event_handle_t cb;
} sl_ble_event_manage_t;

/*==============================================*/
/**
 * @fn         sl_ble_init
 * @brief      this function is used to init to ble app.
 * @return     none.
 * @section description
 */
void sl_ble_init(void);

/*==============================================*/
/**
 * @fn         sli_ble_app_set_event
 * @brief      sets the specific event.
 * @param[in]  evt_id, specific event number.
 * @param[in]  evt_data, specific event data.
 * @param[in]  data_len, specific event data length.
 * @return     status.
 * @section description
 * This function is used to set/raise the specific event.
 */
sl_status_t
sli_ble_app_set_event(uint32_t evt_id, void *evt_data, uint32_t data_len);

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
                             uint16_t data_len);

#endif // SL_BLE_APP_H
