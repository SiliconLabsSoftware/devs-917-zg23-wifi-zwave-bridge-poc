/***************************************************************************/ /**
 * @file sl_zw_send_data.h
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

#ifndef SL_ZW_SEND_DATA_H
#define SL_ZW_SEND_DATA_H

#include <stdint.h>
#include "sl_ts_param.h"

int scheme_compare(security_scheme_t a, security_scheme_t b);

void sl_zw_send_data_appl_rx_notify(const ts_param_t *c,
                                    const uint8_t *frame,
                                    uint16_t length);

int32_t zw_send_data_event_process(uint32_t ev, void *data);

void sl_zw_send_data_appl_init();

bool ZW_SendDataAppl_idle(void);

void ZW_SendDataApplAbort(uint8_t handle);

uint8_t sl_zw_send_data_appl(ts_param_t *p,
                             const void *pData,
                             uint16_t dataLength,
                             ZW_SendDataAppl_Callback_t callback,
                             void *user);

/**
 * Send data to an endpoint and do endpoint encap security encap CRC16 or transport service encap
 * if needed. This function is not reentrant. It will only be called from the sl_zw_send_data_appl event tree
 * @param p
 * @param data
 * @param len
 * @param cb
 * @param user
 * @return
 */
uint8_t send_endpoint(ts_param_t *p,
                      const uint8_t *data,
                      uint16_t len,
                      ZW_SendDataAppl_Callback_t cb,
                      void *user);

/**
 * Low level send data. This call will not do any encapsulation except transport service encap
 */
uint8_t send_data(ts_param_t *p,
                  const uint8_t *data,
                  uint16_t len,
                  ZW_SendDataAppl_Callback_t cb,
                  void *user);

uint8_t ts_param_cmp(ts_param_t *a1, const ts_param_t *a2);

void ts_set_std(ts_param_t *p, nodeid_t dnode);

sl_status_t zw_send_data_get_event(void *msg, uint32_t timeout);

sl_status_t zw_send_data_post_event(uint32_t event, void *data);

void sl_zw_layer_data_process(void);

#endif // SL_ZW_SEND_DATA_H
