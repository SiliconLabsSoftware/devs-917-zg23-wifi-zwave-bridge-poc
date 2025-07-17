/*******************************************************************************
 * @file  ipv46_internal.h
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

#ifndef IPV46_INTERNAL_H_
#define IPV46_INTERNAL_H_

#include "sl_uip_def.h"

/** \defgroup ip4zwmap IPv4 to Z-Wave address translator
 * \ingroup ip46nat
 *
 * This submodule maps between IPv4 addresses and Z-Wave node IDs.
 *
 * The submodule is independent of IPv6 support.
 * @{
 */

/**
 * Get the IPv4 address of a node.
 *
 * @param ip destination to write the IP to
 * @param node ID of the node to query for
 * @return TRUE if address was found
 */
uint8_t ipv46nat_ipv4addr_of_node(uip_ip4addr_t* ip, nodeid_t node);

/**
 * Get the node id behind the IPv4 address.
 *
 * @param ip IPv4 address
 * @return nodeid
 */
nodeid_t ipv46nat_get_nat_addr(uip_ip4addr_t *ip);

/** \defgroup ipv46natint IPv4/IPv6 Internal.
 * \ingroup ip46nat
 *
 * Internal data management of the IPv4/IPv6 support component.
 * Shared with \ref ZIP_DHCP.

 * @{
 */

/**
 * Contains information about each node.
 */
typedef struct nat_table_entry {
  /** Node ID which the nat table entry points to */
  nodeid_t nodeid;
  /** The last bits of the IPv4 address if the nat entry.
   * This is combined with the IPv4 net to form the full IPv4 address.
   */
  uint16_t ip_suffix;
} nat_table_entry_t;

/**
 * Maximum number of entries in the NAT table.
 */
#define MAX_NAT_ENTRIES 32

/**
 * Actual number of entries in the NAT table.
 */
extern uint16_t nat_table_size;

/**
 * The NAT entry database.
 */
extern nat_table_entry_t nat_table[MAX_NAT_ENTRIES];

/* For DHCP */

/**
 * Get the IPv4 address of a nat table entry.
 * @param ip destination to write to
 * @param e Entry to query
 */
void ipv46nat_ipv4addr_of_entry(uip_ip4addr_t* ip, nat_table_entry_t *e);

/** @} */
#endif
