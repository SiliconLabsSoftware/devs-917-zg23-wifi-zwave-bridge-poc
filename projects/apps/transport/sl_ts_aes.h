/*******************************************************************************
 * @file  sl_ts_aes.h
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
#ifndef SL_TS_AES_H
#define SL_TS_AES_H

#include <stdint.h>

void aes_encrypt(uint8_t *in, uint8_t *out);
void aes_set_key(uint8_t* key);
void aes_set_key_tpt(uint8_t *key, uint8_t *iv);
void aes_ofb(uint8_t *data, uint8_t len);
/*
 * Caclucalte the authtag for the message,
 */
void aes_cbc_mac(uint8_t *data, uint8_t len, uint8_t *mac);
/**
 * Generate 8 random bytes
 */
void aes_random8(uint8_t *d);

#endif // SL_TS_AES_H
