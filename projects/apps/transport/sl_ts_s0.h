/***************************************************************************/ /**
 * @file sl_ts_s0.h
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

#ifndef SL_TS_S0_H
#define SL_TS_S0_H

#include "sl_common_type.h"

#define KEY_CLASS_S0                  0x80

#define NONCE_TABLE_SIZE 10 * 3
#define NONCE_TIMEOUT    10

#define NONCE_BLACKLIST_SIZE 10
#define RECEIVERS_NONCE_SIZE 8

typedef struct nonce {
  uint8_t src;
  uint8_t dst;
  uint8_t timeout;
  uint8_t reply_nonce; //indicate if this nonce from a enc message sent by me
  uint8_t nonce[8];
} nonce_t;

/* Nonce blacklist type*/
typedef struct {
  uint8_t nonce[RECEIVERS_NONCE_SIZE];
  uint8_t src;
  uint8_t dst;
  unsigned int in_use;
} nonce_blacklist_t;

/**
 * Test if an S0 nonce with particular source and destination is blacklisted.
 *
 * \return 1 if nonce is in blacklist, 0 otherwise
 */
unsigned int sec0_is_nonce_blacklisted(const uint8_t src,
                                       const uint8_t dst,
                                       const uint8_t *nonce);
/**
 *  We blacklist the last NONCE_BLACKLIST_SIZE external nonces we have used.
 *  This function adds nonce to the blacklist.
 */
void sec0_blacklist_add_nonce(const uint8_t src,
                              const uint8_t dst,
                              const uint8_t *nonce);

void blacklist_reset(void);

/**
 * Register a new nonce from sent from src to dst
 */
uint8_t register_nonce(uint8_t src,
                       uint8_t dst,
                       uint8_t reply_nonce,
                       const uint8_t nonce[8]);

uint8_t has_three_nonces(uint8_t src, uint8_t dst);
/**
 * Receive nonce sent from src to dst, if th ri. If a nonce
 * is found the remove all entries from that src->dst combination
 * from the table.
 *
 * If any_nonce is set then ri is ignored
 */
uint8_t get_nonce(uint8_t src,
                  uint8_t dst,
                  uint8_t ri,
                  uint8_t nonce[8],
                  uint8_t any_nonce);
/**
 * Remove all nonces from nonce table sent from src to dst
 * @param src
 * @param dst
 */
void nonce_clear(uint8_t src, uint8_t dst);

void nonce_init(void);

#endif // SL_TS_S0_H
