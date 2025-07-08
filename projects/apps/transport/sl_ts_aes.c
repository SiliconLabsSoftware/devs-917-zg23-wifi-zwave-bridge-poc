/***************************************************************************/ /**
 * @file sl_ts_aes.c
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

#include <stdint.h>
#include <sl_rd_types.h>
#include "sl_common_log.h"

#include "modules/sl_si917_aes.h"

#include "sl_ts_aes.h"

/************************ AES Helper functions ********************************/
static uint8_t aes_key[16];
static uint8_t aes_iv[16];

void aes_encrypt(uint8_t *in, uint8_t *out)
{
  // SerialAPI_AES128_Encrypt(in,out,aes_key);
  // call aes128 encrypt function. this need implement in your platform.
  sl_si917_aes_encryption(in, 16, aes_key, NULL, out);
}

void aes_set_key(uint8_t* key)
{
  memcpy(aes_key, key, 16);
}

void aes_set_key_tpt(uint8_t *key, uint8_t *iv)
{
  memcpy(aes_key, key, 16);
  memcpy(aes_iv, iv, 16);
}

void aes_ofb(uint8_t *data, uint8_t len)
{
  int i;

  for (i = 0; i < len; i++) {
    if ((i & 0xF) == 0x0) {
      aes_encrypt(aes_iv, aes_iv);
    }
    data[i] ^= aes_iv[(i & 0xF)];
  }
}

/*
 * Caclucalte the authtag for the message,
 */
void aes_cbc_mac(uint8_t *data, uint8_t len, uint8_t *mac)
{
  uint8_t i;

  aes_encrypt(aes_iv, mac);
  for (i = 0; i < len; i++) {
    mac[i & 0xF] ^= data[i];
    if ((i & 0xF) == 0xF) {
      aes_encrypt(mac, mac);
    }
  }

  /*if len is not divisible by 16 do the final pass, this is the padding described in the spec */
  if (len & 0xF) {
    aes_encrypt(mac, mac);
  }
}

/**
 * Generate 8 random bytes
 */
void aes_random8(uint8_t *d)
{
  uint32_t ti = osKernelGetSysTimerCount();
  memcpy(d, (uint8_t*)&ti, 4);
  memcpy(d + 4, (uint8_t*)&ti, 4);
}
