/*******************************************************************************
 * @file  sl_zw_validator.h
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
#ifndef SL_ZW_VALIDATOR_H
#define SL_ZW_VALIDATOR_H

int sl_zw_validator_is_cmd_supporting(uint8_t cls, uint8_t cmd);
int sl_zw_validator_is_cmd_report(uint8_t cls, uint8_t cmd);
int sl_zw_validator_is_cmd_set(uint8_t cls, uint8_t cmd);
int sl_zw_validator_is_cmd_get(uint8_t cls, uint8_t cmd);

#endif // SL_ZW_VALIDATOR_H
