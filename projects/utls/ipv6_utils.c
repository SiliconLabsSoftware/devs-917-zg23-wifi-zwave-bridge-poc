/*******************************************************************************
 * @file  ipv6_utils.c
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
#include "sl_common_config.h"
#include "sl_gw_info.h"
#include "sl_common_log.h"
#include "sl_uip_def.h"
#include "ipv6_utils.h"

nodeid_t ipv46nat_get_nat_addr(uip_ip4addr_t *ip);

/* Z-Wave/IPv6 address utilities. */

/*
 * Get the ipv6 address of a given node
 */
void sl_ip_of_node(uip_ip6addr_t *dst, nodeid_t nodeID)
{
  if (nodeID == MyNodeID) {
    uip_ipaddr_copy(dst, &router_cfg.lan_addr);
  } else {
    uip_ipaddr_copy(dst, &router_cfg.pan_prefix);
    dst->u16[7] = swaps(nodeID);//UIP_HTONS(nodeID);
  }
}

/**
 * Get the mac address of a node. For #MyNodeID this returns
 * the device mac address which should be globally unique.
 * For the Z-Wave nodes we use locally administered MAC addresses.
 *
 * The locally administered address MUST be created in a way so
 * multiple Z/IP gateways can coincide in the same network. If the
 * gateway are member of the same Z-Wave network(HomeID) they will use the
 * same MAC range.
 */
void macOfNode(uip_lladdr_t *dst, nodeid_t nodeID)
{
  if (nodeID == MyNodeID) {
    memcpy(dst, &sl_gw_info.mac_addr, sizeof(uip_lladdr_t));
  } else {
    dst->addr[0] = ((homeID >> 24) & 0xff);
    dst->addr[1] = ((homeID >> 16) & 0xff);
    dst->addr[2] = (homeID >> 8) & 0xFF;
    dst->addr[3] = (homeID & 0xFF);
    dst->addr[4] = (nodeID >> 8) & 0xFF;
    dst->addr[5] = nodeID & 0xFF;
  }
}

/**
 * Return the node is corresponding to an ip address, returns 0 if the
 * ip address is not in the pan
 */
nodeid_t sl_node_of_ip(uip_ip6addr_t *ip)
{
  if (uip_ipaddr_prefixcmp(ip, &router_cfg.pan_prefix, 64)) {
    return swaps(ip->u16[7]);
  }

  if (memcmp(ip->u8, &router_cfg.lan_addr.u8, sizeof(router_cfg.lan_addr)) == 0) {
    LOG_PRINTF("myNodeID: %d\n", MyNodeID);
    return MyNodeID;
  }

  if (uip_is_4to6_addr(ip)) {
    return ipv46nat_get_nat_addr((uip_ip4addr_t *) &ip->u8[12]);
  }
  return 0;
}

/* ******* IP/LAN and zip_router_config ******** */

/**
 * Reread all IPv6 addresses from router_cfg structure
 */
void refresh_ipv6_addresses()
{
  // Reserved for future use
}

void
uip_debug_ipaddr_print(const uip_ipaddr_t *addr)
{
  uint16_t a;
  uint8_t i, f;
  for (i = 0, f = 0; i < sizeof(uip_ipaddr_t); i += 2) {
    a = (addr->u8[i] << 8) + addr->u8[i + 1];
    if (a == 0 && f > 0) {
      if (f++ == 0) {
        LOG_PRINTF(":");
        LOG_PRINTF(":");
      }
    } else {
      if (f > 0) {
        f = -1;
      } else if (i > 0) {
        LOG_PRINTF(":");
      }
      LOG_PRINTF("%02x", a);
    }
  }
  // LOG_PRINTF("\n");
}

void uip_debug_ip4addr_print(const uip_ip4addr_t *addr)
{
  LOG_PRINTF("%u.%u.%u.%u", addr->u8[0], addr->u8[1], addr->u8[2], addr->u8[3]);
}
/*---------------------------------------------------------------------------*/
void
uip_debug_lladdr_print(const uip_lladdr_t *addr)
{
  uint8_t i;
  for (i = 0; i < sizeof(uip_lladdr_t); i++) {
    if (i > 0) {
      LOG_PRINTF(":");
    }
    LOG_PRINTF("%02x", addr->addr[i]);
  }
  // LOG_PRINTF("\n");
}
