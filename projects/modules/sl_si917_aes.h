/*******************************************************************************
 * @file  sl_si917_aes.h
 * @brief aes functions header file.
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
#ifndef SL_SI917_AES_H
#define SL_SI917_AES_H

#include <stdint.h>

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
                           uint8_t *msg_out);

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
                           uint8_t *msg_out);

/**
 * @brief Initialize the SI917 AES module.
 *
 * Placeholder for any required AES hardware or software initialization.
 */
void sl_si917_aes_init(void);

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
                                uint8_t *msg_out);

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
                                uint8_t *msg_out);

#endif // SL_SI917_AES_H
