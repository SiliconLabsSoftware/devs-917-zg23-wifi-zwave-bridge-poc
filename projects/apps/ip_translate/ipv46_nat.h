/*******************************************************************************
 * @file  ipv46_nat.h
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
#ifndef IPV46_NAT_H_
#define IPV46_NAT_H_

/**
 * \defgroup ip46nat IPv4 to IPv6 network address translator.
 *
 * This module contains supporting features that allows the gateway to understand and use IPv4 addresses.
 *
 * These features are used by the Z/IP Gateway processes \ref
 * ZIP_DHCP, \ref ZIP_ipv4, \ref Portal_handler and in Contiki.
 *
 * The module maps IPv4 UDP and ICMP ping packages into their IPv6
 * equivalents and vice versa. This is done to allow the internal Z/IP
 * Gateway IPv6 logic to process the IPv4 frames as well.
 *
 * It also contains the data-structures that hold the IPv4 addresses
 * for the Z-Wave network nodes.
 *
 * @{
 */

/**
 * Add NAT table entry.  Start DHCP on the new node.
 *
 * \return 0 if the add entry fails
 */
uint8_t ipv46nat_add_entry(nodeid_t node);

/**
 * Remove a NAT table entry, return 1 if the entry was removed.
 */
uint8_t ipv46nat_del_entry(nodeid_t node);

/**
 * Remove and send out DHCP RELEASE for all nat table entries except the ZIPGW.
 *
 * This is called when the pan side of the GW leaves one network, eg,
 * in order to join another one.  In the new network, the nodes will
 * have new ids, so they should also get new IPv4 addresses.  The GW
 * itself does not change DHCP client ID, so it does not need a new
 * DHCP lease.
 * This includes gateway reset and learn mode.
 */
void ipv46nat_del_all_nodes();

/**
 * Initialize the IPv4to6 translator.
 */
void ipv46nat_init();

/**
 * Return true if all nodes has an IP.
 */
uint8_t ipv46nat_all_nodes_has_ip();

/**
 * Rename a node in the nat table to a new ID.
 */
void ipv46nat_rename_node(nodeid_t old_id, nodeid_t new_id);

/**
 * @}
 */

#endif /* IPV46_NAT_H_ */
