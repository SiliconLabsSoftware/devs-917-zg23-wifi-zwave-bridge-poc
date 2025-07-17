/*******************************************************************************
 * @file  sl_si917_aes.c
 * @brief aes functions
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
#include <string.h>
#include "cmsis_os2.h"
#include "sl_common_log.h"
#include "sl_common_type.h"

#if defined(SL_USE_LWIP_STACK)
#include "mbedtls/aes.h"
#else
#include "sl_si91x_aes.h"
#include "sl_si91x_crypto_utility.h"
#endif // defined(SL_USE_LWIP_STACK)

/******************************************************
*                    Constants
******************************************************/

/**
 * @brief Encrypt data using AES-128 ECB mode with mbedTLS.
 *
 * Encrypts the input message using AES-128 in ECB mode via mbedTLS.
 * The input length must be a multiple of 16 bytes.
 *
 * @param msg      Pointer to the input data to encrypt.
 * @param msg_len  Length of the input data in bytes (must be multiple of 16).
 * @param key      Pointer to the 128-bit (16-byte) AES key.
 * @param iv       Pointer to the IV (not used in ECB mode, can be NULL).
 * @param msg_out  Pointer to the output buffer for encrypted data.
 * @return 0 on success, -1 on failure.
 */
int sl_mbed_aes_encryption(uint8_t *msg,
                           uint16_t msg_len,
                           uint8_t *key,
                           uint8_t *iv,
                           uint8_t *msg_out)
{
  (void) iv;
  if (msg_len % 16 != 0) {
    LOG_PRINTF("AES input length must be multiple of 16 bytes\n");
    return -1;
  }
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, key, 128);
  if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, msg, msg_out) != 0) {
    ERR_PRINTF("\r\nAES encryption failed\r\n");
    mbedtls_aes_free(&aes);
    return -1;
  }
  mbedtls_aes_free(&aes);
  return 0;
}

/**
 * @brief Decrypt data using AES-128 ECB mode with mbedTLS.
 *
 * Decrypts the input message using AES-128 in ECB mode via mbedTLS.
 * The input length must be a multiple of 16 bytes.
 *
 * @param msg      Pointer to the input data to decrypt.
 * @param msg_len  Length of the input data in bytes (must be multiple of 16).
 * @param key      Pointer to the 128-bit (16-byte) AES key.
 * @param iv       Pointer to the IV (not used in ECB mode, can be NULL).
 * @param msg_out  Pointer to the output buffer for decrypted data.
 * @return 0 on success, -1 on failure.
 */
int sl_mbed_aes_decryption(uint8_t *msg,
                           uint16_t msg_len,
                           uint8_t *key,
                           uint8_t *iv,
                           uint8_t *msg_out)
{
  (void) iv;
  if (msg_len % 16 != 0) {
    LOG_PRINTF("AES input length must be multiple of 16 bytes\n");
    return -1;
  }
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_dec(&aes, key, 128);
  if (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, msg, msg_out) != 0) {
    LOG_PRINTF("\r\nAES decryption failed\r\n");
    mbedtls_aes_free(&aes);
    return -1;
  }
  mbedtls_aes_free(&aes);
  return 0;
}

/**
 * @brief Initialize the SI917 AES module.
 *
 * Placeholder for any required AES hardware or software initialization.
 */
void sl_si917_aes_init(void)
{
  LOG_PRINTF("aes init\n");
}

/**
 * @brief Encrypt data using AES-128 ECB mode (hardware or mbedTLS).
 *
 * Encrypts the input message using AES-128 in ECB mode, using either
 * the SI91x hardware AES engine or mbedTLS, depending on configuration.
 * The input length must be a multiple of 16 bytes.
 *
 * @param msg      Pointer to the input data to encrypt.
 * @param msg_len  Length of the input data in bytes (must be multiple of 16).
 * @param key      Pointer to the 128-bit (16-byte) AES key.
 * @param iv       Pointer to the IV (not used in ECB mode, can be NULL).
 * @param msg_out  Pointer to the output buffer for encrypted data.
 * @return 0 on success, -1 on failure.
 */
int32_t sl_si917_aes_encryption(uint8_t *msg,
                                uint16_t msg_len,
                                uint8_t *key,
                                uint8_t *iv,
                                uint8_t *msg_out)
{
#ifdef SL_USE_LWIP_STACK
  return sl_mbed_aes_encryption(msg, msg_len, key, iv, msg_out);
#else
  sl_status_t status;
  sl_si91x_aes_config_t config;
  memset(&config, 0, sizeof(sl_si91x_aes_config_t));

  config.aes_mode                   = SL_SI91X_AES_ECB;
  config.encrypt_decrypt            = SL_SI91X_AES_ENCRYPT;
  config.msg                        = msg;
  config.msg_length                 = msg_len;
  config.iv                         = iv;
  config.key_config.b0.key_size     = SL_SI91X_AES_KEY_SIZE_128;
  config.key_config.b0.key_slot     = 0;
  config.key_config.b0.wrap_iv_mode = SL_SI91X_WRAP_IV_ECB_MODE;
  config.key_config.b0.key_type     = SL_SI91X_TRANSPARENT_KEY;
  memcpy(config.key_config.b0.key_buffer, key, config.key_config.b0.key_size);
  osMutexAcquire(sl_aes_mutex, 0xFFFFFFFFUL);
  status = sl_si91x_aes(&config, msg_out);
  osMutexRelease(sl_aes_mutex);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\r\nAES encryption failed, Error Code : 0x%lX\r\n", status);
    return -1;
  }
  LOG_PRINTF("\r\nAES encryption success\r\n");

  return 0;
#endif // SL_USE_LWIP_STACK
}

/**
 * @brief Decrypt data using AES-128 ECB mode (hardware or mbedTLS).
 *
 * Decrypts the input message using AES-128 in ECB mode, using either
 * the SI91x hardware AES engine or mbedTLS, depending on configuration.
 * The input length must be a multiple of 16 bytes.
 *
 * @param msg      Pointer to the input data to decrypt.
 * @param msg_len  Length of the input data in bytes (must be multiple of 16).
 * @param key      Pointer to the 128-bit (16-byte) AES key.
 * @param iv       Pointer to the IV (not used in ECB mode, can be NULL).
 * @param msg_out  Pointer to the output buffer for decrypted data.
 * @return 0 on success, -1 on failure.
 */
int32_t sl_si917_aes_decryption(uint8_t *msg,
                                uint16_t msg_len,
                                uint8_t *key,
                                uint8_t *iv,
                                uint8_t *msg_out)
{
#ifdef SL_USE_LWIP_STACK
  return sl_mbed_aes_decryption(msg, msg_len, key, iv, msg_out);
#else
  sl_status_t status;
  sl_si91x_aes_config_t config;
  memset(&config, 0, sizeof(sl_si91x_aes_config_t));

  config.aes_mode                   = SL_SI91X_AES_ECB;
  config.encrypt_decrypt            = SL_SI91X_AES_DECRYPT;
  config.msg                        = msg;
  config.msg_length                 = msg_len;
  config.iv                         = iv;
  config.key_config.b0.key_size     = SL_SI91X_AES_KEY_SIZE_128;
  config.key_config.b0.key_slot     = 0;
  config.key_config.b0.wrap_iv_mode = SL_SI91X_WRAP_IV_ECB_MODE;
  config.key_config.b0.key_type     = SL_SI91X_TRANSPARENT_KEY;
  memcpy(config.key_config.b0.key_buffer, key, config.key_config.b0.key_size);

  osMutexAcquire(sl_aes_mutex, 0xFFFFFFFFUL);
  status = sl_si91x_aes(&config, msg_out);
  osMutexRelease(sl_aes_mutex);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("\r\nAES decryption failed, Error Code : 0x%lX\r\n", status);
    return -1;
  }
  LOG_PRINTF("\r\nAES decryption success\r\n");

  return 0;
#endif // SL_USE_LWIP_STACK
}
