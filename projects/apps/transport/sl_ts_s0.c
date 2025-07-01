/***************************************************************************/ /**
 * @file sl_ts_s0.c
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
#include "sl_common_type.h"
#include "sl_common_log.h"

#include "sl_ts_s0.h"

nonce_blacklist_t nonce_blacklist[NONCE_BLACKLIST_SIZE];

/* Cyclic counter for the next element in nonce blacklist */
static unsigned int blacklist_next_elem;

//Nonces received or sent
static nonce_t nonce_table[NONCE_TABLE_SIZE];
static osTimerId_t nonce_timer;

/******************************** Nonce Blacklist *****************************/
/**
 * Test if an S0 nonce with particular source and destination is blacklisted.
 *
 * \return 1 if nonce is in blacklist, 0 otherwise
 */
unsigned int sec0_is_nonce_blacklisted(const uint8_t src,
                                       const uint8_t dst,
                                       const uint8_t *nonce)
{
  for (unsigned int i = 0; i < NONCE_BLACKLIST_SIZE; i++) {
    if (nonce_blacklist[i].in_use
        && (0 == memcmp(nonce_blacklist[i].nonce, nonce, 8))
        && (nonce_blacklist[i].src == src) && (nonce_blacklist[i].dst == dst)) {
      return 1;
    }
  }
  return 0;
}

/**
 *  We blacklist the last NONCE_BLACKLIST_SIZE external nonces we have used.
 *  This function adds nonce to the blacklist.
 */
void sec0_blacklist_add_nonce(const uint8_t src,
                              const uint8_t dst,
                              const uint8_t *nonce)
{
  nonce_blacklist[blacklist_next_elem].in_use = 1;
  memcpy(nonce_blacklist[blacklist_next_elem].nonce,
         nonce,
         RECEIVERS_NONCE_SIZE);
  nonce_blacklist[blacklist_next_elem].src = src;
  nonce_blacklist[blacklist_next_elem].dst = dst;
  blacklist_next_elem = (blacklist_next_elem + 1) % NONCE_BLACKLIST_SIZE;
}

void blacklist_reset(void)
{
  for (int i = 0; i < NONCE_BLACKLIST_SIZE; i++) {
    memset(&nonce_blacklist[i], 0, sizeof(nonce_blacklist_t));
  }
  blacklist_next_elem = 0;
}

/******************************** Nonce Management **********************************************/
/**
 * Register a new nonce from sent from src to dst
 */
uint8_t register_nonce(uint8_t src,
                       uint8_t dst,
                       uint8_t reply_nonce,
                       const uint8_t nonce[8])
{
  uint8_t i;

  // DBG_PRINTF("register_nonce: ");
  // sl_print_hex_to_string((uint8_t*)nonce, 8);
  // DBG_PRINTF("\n");
  if (reply_nonce) {
    /*Only one reply nonce is allowed*/
    for (i = 0; i < NONCE_TABLE_SIZE; i++) {
      if (nonce_table[i].reply_nonce && nonce_table[i].timeout > 0
          && nonce_table[i].src == src && nonce_table[i].dst == dst) {
        DBG_PRINTF("Reply nonce overwritten\n");
        memcpy(nonce_table[i].nonce, nonce, 8);
        nonce_table[i].timeout = NONCE_TIMEOUT;
        return 1;
      }
    }
  }

  for (i = 0; i < NONCE_TABLE_SIZE; i++) {
    if (nonce_table[i].timeout == 0) {
      nonce_table[i].src         = src;
      nonce_table[i].dst         = dst;
      nonce_table[i].reply_nonce = reply_nonce;
      memcpy(nonce_table[i].nonce, nonce, 8);
      nonce_table[i].timeout = NONCE_TIMEOUT;
      return 1;
    }
  }

  ERR_PRINTF("Nonce table is full\n");
  return 0;
}

uint8_t has_three_nonces(uint8_t src, uint8_t dst)
{
  uint8_t nonce_count = 0;
  for (int i = 0; i < NONCE_TABLE_SIZE; i++) {
    if (nonce_table[i].timeout > 0 && nonce_table[i].src == src
        && nonce_table[i].dst == dst) {
      nonce_count++;
    }
  }
  if (nonce_count == 3) {
    return 1;
  }
  return 0;
}

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
                  uint8_t any_nonce)
{
  uint8_t i;

  for (i = 0; i < NONCE_TABLE_SIZE; i++) {
    if (nonce_table[i].timeout > 0 && nonce_table[i].src == src
        && nonce_table[i].dst == dst) {
      if (any_nonce || nonce_table[i].nonce[0] == ri) {
        memcpy(nonce, nonce_table[i].nonce, 8);
        return 1;
      }
    }
  }
  return 0;
}

/**
 * Remove all nonces from nonce table sent from src to dst
 * @param src
 * @param dst
 */
void nonce_clear(uint8_t src, uint8_t dst)
{
  uint8_t i;
  /*Remove entries from table from that source dest combination */
  for (i = 0; i < NONCE_TABLE_SIZE; i++) {
    if (nonce_table[i].timeout && nonce_table[i].src == src
        && nonce_table[i].dst == dst) {
      nonce_table[i].timeout = 0;
    }
  }
}

void nonce_timer_timeout(void *data)
{
  (void) data;
  u8_t i;
  for (i = 0; i < NONCE_TABLE_SIZE; i++) {
    if (nonce_table[i].timeout > 0) {
      nonce_table[i].timeout--;
    }
  }
}

void nonce_init(void)
{
  /* We do not re-initialize the nonce table here, because it is
   * actually desirable to keep the nonces after a gateway reset. The nonces will
   * timeout anyway. This fixes a situation like TO#05875, where the gateway get a nonce
   * get just before it resets after learnmode has completed.
   */
  int i = 0;
  for (i = 0; i < NONCE_TABLE_SIZE; i++) {
    nonce_table[i].timeout = 0;
  }
  nonce_timer = osTimerNew(nonce_timer_timeout, osTimerPeriodic, NULL, NULL);
  osTimerStart(nonce_timer, 1000);
}
