/*******************************************************************************
 * @file  ipv46_nat.c
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
#include "string.h"
#include "sl_uip_def.h"
#include "sl_common_log.h"
#include "sl_gw_info.h"
#include "ipv46_internal.h"
#include "ipv46_nat.h"

#include "ZW_typedefs.h"
#include "ZW_classcmd.h"
#include "sl_rd_types.h"

#include "modules/sl_si917_net.h"

uint16_t  nat_table_size = 0;
nat_table_entry_t nat_table[MAX_NAT_ENTRIES];
void ipv46nat_ipv4addr_of_entry(uip_ip4addr_t* ip, nat_table_entry_t *e);

/**
 * @return The node id behind the ipv4 address
 * Return NULL if this is not one of our ipaddresses
 */
nodeid_t ipv46nat_get_nat_addr(uip_ip4addr_t *ip)
{
  uint16_t i;
  uint8_t *a1 = ip->u8;
  uint8_t *a2 = sl_gw_info.ip_address.ip.v4.bytes;
  uint8_t *m = sl_gw_info.subnet;

  if (uip_ipaddr_maskcmp_u8(a1, a2, m)) {
    for (i = 0; i < nat_table_size; i++) {
      uint16_t u16 = (sl_gw_info.subnet[2] << 8) | sl_gw_info.subnet[3];
      if ( (nat_table[i].ip_suffix) == (ip->u16[1] & (~u16))) {
        return nat_table[i].nodeid;
      }
    }
  }
  return 0;
}

/**
 * Add NAT table entry. Returns 0 is the add entry fails
 */
uint8_t ipv46nat_add_entry(nodeid_t node)
{
  uint16_t i;
  if (nat_table_size >= MAX_NAT_ENTRIES) {
    return 0;
  }

  for (i = 0; i < nat_table_size; i++) {
    if ( (nat_table[i].nodeid) == node ) {
      LOG_PRINTF("Entry %d already exists\n", node);
      return nat_table_size;
    }
  }

  nat_table[nat_table_size].nodeid = node;
  LOG_PRINTF("Nat entry added node = %d ip = %u\n",
             nat_table[nat_table_size].nodeid, (unsigned)UIP_HTONS(nat_table[nat_table_size].ip_suffix));

  nat_table_size++;
  return nat_table_size;
}

/**
 * Remove a nat table entry, return 1 if the entry was removed.
 */
uint8_t ipv46nat_del_entry(nodeid_t node)
{
  uint16_t i, removing;

  if (node == MyNodeID) {
    return 0;
  }
  removing = 0;
  for (i = 0; i < nat_table_size; i++) {
    if (removing) {
      nat_table[i - 1] = nat_table[i];
    } else if (nat_table[i].nodeid == node) {
      LOG_PRINTF("Releasing DHCP entry for node %d\n", node);
      removing++;
    }
  }
  nat_table_size -= removing;
  return removing;
}

/**
 * Remove all nat table entries except the gw.
 */
void ipv46nat_del_all_nodes()
{
  uint16_t i;
  uint16_t my_index = 0;

  for (i = 0; i < nat_table_size; i++) {
    if (nat_table[i].nodeid == MyNodeID) {
      my_index = i;
    } else {
      LOG_PRINTF("Releasing DHCP entry for node %d\n",
                 nat_table[i].nodeid);
    }
  }
  nat_table[0] = nat_table[my_index];
  nat_table_size = 1;
}

void ipv46nat_init()
{
  nat_table_size = 0;
}

uint8_t ipv46nat_ipv4addr_of_node(uip_ip4addr_t* ip, nodeid_t node)
{
  uint16_t i;

  for (i = 0; i < nat_table_size; i++) {
    LOG_PRINTF("%d of %d %d\n", i, nat_table[i].nodeid, node);
    if (nat_table[i].nodeid == node && nat_table[i].ip_suffix) {
      ipv46nat_ipv4addr_of_entry(ip, &nat_table[i]);
      return 1;
    }
  }
  return 0;
}

void ipv46nat_ipv4addr_of_entry(uip_ip4addr_t* ip, nat_table_entry_t *e)
{
  memcpy(ip, sl_gw_info.ip_address.ip.v4.bytes, sizeof(uip_ip4addr_t));
  uint16_t u16 = (sl_gw_info.subnet[2] << 8) | sl_gw_info.subnet[3];
  ip->u16[1] &= u16;
  ip->u16[1] |= e->ip_suffix;
  return;
}

uint8_t ipv46nat_all_nodes_has_ip()
{
  uint16_t i;

  for (i = 0; i < nat_table_size; i++) {
    if (nat_table[i].ip_suffix == 0) {
      return 0;
    }
  }
  return 1;
}

void ipv46nat_rename_node(nodeid_t old_id, nodeid_t new_id)
{
  uint16_t i;

  /* The gateway should always have index 0, no matter
   * what MyNodeID is, so renaming the gateway itself should be simple
   * and it should never be necessary to delete it. */
  for (i = 0; i < nat_table_size; i++) {
    if (nat_table[i].nodeid == old_id) {
      nat_table[i].nodeid = new_id;
    }
  }
}
