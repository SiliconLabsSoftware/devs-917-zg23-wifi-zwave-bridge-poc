/*******************************************************************************
 * @file  ipv46_if_handler.c
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
#include "sl_common_log.h"
#include "sl_gw_info.h"
#include "ipv46_nat.h"
#include "ipv46_internal.h"

/* TYPES.H must be included before ZW_classcmd.h */
#include "sl_common_type.h"
#include "ZW_classcmd.h"

#include "ipv46_if_handler.h"
#include "sl_rd_types.h"

#define uip_ds6_is_my_addr(addr)  (uip_ds6_addr_lookup(addr) != NULL)
#define uip_ds6_is_my_maddr(addr) (uip_ds6_maddr_lookup(addr) != NULL)

uint8_t is_4to6_addr(const uip_ip6addr_t* ip);

void ip4to6_addr(uip_ip6addr_t* dst, uip_ip4addr_t* src)
{
  dst->u16[0] = 0;
  dst->u16[1] = 0;
  dst->u16[2] = 0;
  dst->u16[3] = 0;
  dst->u16[4] = 0;
  dst->u16[5] = 0xFFFF;
  dst->u16[6] = src->u16[0];
  dst->u16[7] = src->u16[1];
}

/*
 * Return true if this is an IPv4 mapped IPv6 address
 */
uint8_t is_4to6_addr(const uip_ip6addr_t* ip)
{
  return(
    ip->u16[0] == 0
    && ip->u16[1] == 0
    && ip->u16[2] == 0
    && ip->u16[3] == 0
    && ip->u16[4] == 0
    && ip->u16[5] == 0xFFFF);
}
