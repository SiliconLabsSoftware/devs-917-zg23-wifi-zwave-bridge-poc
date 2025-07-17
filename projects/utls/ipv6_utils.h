/*******************************************************************************
 * @file  ipv6_utils.h
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

#ifndef ZIP_ROUTER_IP_UTILS_H_
#define ZIP_ROUTER_IP_UTILS_H_

#include <stdint.h>
#include "sl_rd_types.h"
#include "sl_uip_def.h"

/**
 * Get the ipaddress of a given node
 * \param dst output
 * \param nodeID ID of the node
 */
void sl_ip_of_node(uip_ip6addr_t* dst, nodeid_t nodeID);

/**
 * Return the node is corresponding to an ip address
 * \param ip IP address
 * \return ID of the node
 */
nodeid_t sl_node_of_ip(uip_ip6addr_t *ip);

void macOfNode(uip_lladdr_t* dst, nodeid_t nodeID);

void refresh_ipv6_addresses();

void uip_debug_ipaddr_print(const uip_ipaddr_t *addr);
void uip_debug_ip4addr_print(const uip_ip4addr_t *addr);

void
uip_debug_lladdr_print(const uip_lladdr_t *addr);

#endif
