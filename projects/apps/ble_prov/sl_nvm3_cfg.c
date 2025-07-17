/***************************************************************************/ /**
 * @file sl_nvm3_cfg.c
 * @brief module for nvm3 configuration
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

#include <stdint.h>
#include "sl_status.h"
#include "sl_common_type.h"
#include "nvm3_default.h"
#include "nvm3_default_config.h"

#define NVM3_DEFAULT_HANDLE nvm3_defaultHandle
// Maximum number of data objects saved
#define MAX_OBJECT_COUNT 10

// Max and min keys for data objects
#define MIN_DATA_KEY NVM3_KEY_MIN
#define MAX_DATA_KEY (MIN_DATA_KEY + MAX_OBJECT_COUNT - 1)

// Key of write counter object
#define WRITE_COUNTER_KEY MAX_OBJECT_COUNT

// Key of delete counter object
#define DELETE_COUNTER_KEY (WRITE_COUNTER_KEY + 1)

#define SL_AP_HAS_STORE_KEY  2000
#define SL_AP_SSID_STORE_KEY 2001
#define SL_AP_PWD_STORE_KEY  2002
#define SL_AP_SEC_STORE_KEY  2003

sl_status_t sl_store_init(void)
{
  sl_status_t err;
  err = nvm3_initDefault();
  return err;
}

sl_status_t sl_store_wifi_write(sl_ap_t ap_info)
{
  uint8_t ap_has = 1;
  if (ECODE_NVM3_OK != nvm3_writeData(NVM3_DEFAULT_HANDLE,
                                      SL_AP_HAS_STORE_KEY,
                                      (unsigned char *) &ap_has,
                                      1)) {
    return SL_STATUS_ALLOCATION_FAILED;
  }
  // store ssid.
  if (ECODE_NVM3_OK != nvm3_writeData(NVM3_DEFAULT_HANDLE,
                                      SL_AP_SSID_STORE_KEY,
                                      (unsigned char *) ap_info.ssid,
                                      sizeof(ap_info.ssid))) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  if (ECODE_NVM3_OK != nvm3_writeData(NVM3_DEFAULT_HANDLE,
                                      SL_AP_PWD_STORE_KEY,
                                      (unsigned char *) ap_info.pwd,
                                      sizeof(ap_info.ssid))) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  if (ECODE_NVM3_OK != nvm3_writeData(NVM3_DEFAULT_HANDLE,
                                      SL_AP_SEC_STORE_KEY,
                                      (unsigned char *) &ap_info.sec_type,
                                      sizeof(ap_info.sec_type))) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  return SL_STATUS_OK;
}

sl_status_t sl_store_wifi_check(void)
{
  sl_status_t err;
  uint8_t ap_has;
  err = nvm3_readData(NVM3_DEFAULT_HANDLE, SL_AP_HAS_STORE_KEY, &ap_has, 1);
  if (err != SL_STATUS_OK) {
    return SL_STATUS_ALLOCATION_FAILED;
  }
  if (ap_has != 1) {
    return SL_STATUS_ALLOCATION_FAILED;
  }
  return SL_STATUS_OK;
}

sl_status_t sl_store_wifi_read(sl_ap_t *ap_info)
{
  if (ECODE_NVM3_OK != nvm3_readData(NVM3_DEFAULT_HANDLE,
                                     SL_AP_SSID_STORE_KEY,
                                     (unsigned char *) ap_info->ssid,
                                     sizeof(ap_info->ssid))) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  if (ECODE_NVM3_OK != nvm3_readData(NVM3_DEFAULT_HANDLE,
                                     SL_AP_PWD_STORE_KEY,
                                     (unsigned char *) ap_info->pwd,
                                     sizeof(ap_info->ssid))) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  if (ECODE_NVM3_OK != nvm3_readData(NVM3_DEFAULT_HANDLE,
                                     SL_AP_SEC_STORE_KEY,
                                     (unsigned char *) &ap_info->sec_type,
                                     sizeof(ap_info->sec_type))) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  return SL_STATUS_OK;
}

sl_status_t sl_store_wifi_del(void)
{
  if (ECODE_NVM3_OK
      != nvm3_deleteObject(NVM3_DEFAULT_HANDLE, SL_AP_HAS_STORE_KEY)) {
    return SL_STATUS_ALLOCATION_FAILED;
  }

  //  if (ECODE_NVM3_OK != nvm3_deleteObject(NVM3_DEFAULT_HANDLE, SL_AP_SSID_STORE_KEY)) {
  //        return SL_STATUS_ALLOCATION_FAILED;
  //    }
  //
  //    if (ECODE_NVM3_OK != nvm3_deleteObject(NVM3_DEFAULT_HANDLE, SL_AP_PWD_STORE_KEY)) {
  //          return SL_STATUS_ALLOCATION_FAILED;
  //      }
  //
  //    if (ECODE_NVM3_OK != nvm3_deleteObject(NVM3_DEFAULT_HANDLE, SL_AP_SEC_STORE_KEY)) {
  //          return SL_STATUS_ALLOCATION_FAILED;
  //      }

  return SL_STATUS_OK;
}
