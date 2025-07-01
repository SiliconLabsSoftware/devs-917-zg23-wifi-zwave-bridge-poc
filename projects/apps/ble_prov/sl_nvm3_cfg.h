/***************************************************************************/ /**
 * @file sl_nvm3_cfg.h
 * @brief module nvm3 configuration header file
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

#ifndef SL_NVM3_CFG_H
#define SL_NVM3_CFG_H

sl_status_t sl_store_wifi_read(sl_ap_t *ap_info);
sl_status_t sl_store_wifi_check(void);
sl_status_t sl_store_wifi_write(sl_ap_t ap_info);
sl_status_t sl_store_init(void);
sl_status_t sl_store_wifi_del(void);

#endif /* SL_NVM3_CFG_H */
