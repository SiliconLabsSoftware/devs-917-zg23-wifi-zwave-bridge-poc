/***************************************************************************/ /**
 * @file sl_ts_scheme0.h
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

#ifndef SECURITY_SCHEME0_H_
#define SECURITY_SCHEME0_H_

#include <stdint.h> /* uint8_t */

/** \ingroup transport
 * \defgroup Security_Scheme Security Scheme 0
 *
 * Implementation of Security Scheme 0: these functions handle the inclusion protocol and the secure transmission
 * and receive sessions.
 *
 * @{
 */

/**
 * Register a nonce by source and destination.
 */
void sec0_register_nonce(uint8_t src, uint8_t dst, const  uint8_t* nonce);
uint8_t sec0_send_data(ts_param_t* p, const void* data, uint8_t len, ZW_SendDataAppl_Callback_t callback, void* user);

/**
 * Set the network key used.
 */
void sec0_set_key(uint8_t* netkey);

/**
 * Generate a new net key.
 */
void sec0_reset_netkey();

/**
 * Decrypt an encrypted message, returning the length of the decrypted message. The decrypted message
 * is written into dec_message.
 * @param snode source node
 * @param dnode destination node
 * @param enc_data pointer to the encrypted data, enc_data is modified by this function an will contain a pater of the decrypted message when this function returns
 * @param enc_data_length Length of the encrypted data.
 * @param dec_message The decrypted message is written into this pointer
 * @return The length of the decrypted message, 0 if decryption fails.
 *
 */
uint8_t sec0_decrypt_message(uint8_t snode, uint8_t dnode, const uint8_t* enc_data, uint8_t enc_data_length, uint8_t* dec_message);

/**
 * Send a nonce to the snode in src.
 * \param  src the nonce is sent to src->snode
 */
void sec0_send_nonce(ts_param_t *src);

/**
 * Initialize the security transport layer, which resets all sessions and stops all timers.
 */
void sec0_init();

/**
 * Cancel all transmit sessions.
 */
void sec0_abort_all_tx_sessions();

/**
   The master key, used by the security layer.
 */
extern uint8_t networkKey[16];

/** @} */
#endif /* SECURITY_SCHEME0_H_ */
