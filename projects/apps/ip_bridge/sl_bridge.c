/*******************************************************************************
 * @file  sl_bridge.c
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

#include <lib/list.h>
#include <lib/memb.h>
#ifndef ZIP_NATIVE
#include <stdio.h>
#endif
#include "apps/Common/sl_gw_info.h"
#include "sl_classic_zip_node.h"
#include "ZW_controller_bridge_api.h"
#include "ZW_zip_classcmd.h"
#include "sl_bridge.h"
#include "ZW_udp_server.h"
#include "sl_common_log.h"
#include "utls/zgw_nodemask.h"
#include "utls/ipv6_utils.h"
#include "sl_bridge_temp_assoc.h"
#include "sl_bridge_ip_assoc.h"
#include "sl_status.h"
#include "sl_router_events.h"

sl_status_t zw_zip_post_event(uint32_t event, void *data);
// Global variable shared with multiple modules
bridge_state_t bridge_state;

/** Bitmask reflecting the virtual nodes in the controller */
uint8_t virtual_nodes_mask[MAX_CLASSIC_NODEMASK_LENGTH];

/* Documented in sl_bridge.h */
BOOL is_assoc_create_in_progress(void)
{
  /* We are only checking the state of IP association creation here. The
   * creation of temporary associations does not involve any asynchronously
   * processing so it simply runs to completion before any other events will be
   * handled.
   */

  return (is_ip_assoc_create_in_progress()) ? TRUE : FALSE;
}

/* Inspect a Z-Wave frame payload and extract the destination multichannel endpoint.
 * Return 0 if the frame is not multichannel encapsultaed.
 * Return 0xFF if the frame bit-addresses several endpoints.
 *
 * Known bugs:
 * Returns 0 if fail if there are other encapsulations outside the multichannel
 * (e.g. multi command class).
 *
 */
uint8_t get_multichannel_dest_endpoint(unsigned char *p)
{
  ZW_MULTI_CHANNEL_CMD_ENCAP_V2_FRAME *pCmd = (void *) p;
  if (pCmd->cmdClass == COMMAND_CLASS_MULTI_CHANNEL_V2
      && pCmd->cmd == MULTI_CHANNEL_CMD_ENCAP_V2) {
    if (pCmd->properties2
        & MULTI_CHANNEL_CMD_ENCAP_PROPERTIES2_BIT_ADDRESS_BIT_MASK_V2) {
      return 0xFF;
    }
    return pCmd->properties2
           & MULTI_CHANNEL_CMD_ENCAP_PROPERTIES2_DESTINATION_END_POINT_MASK_V2;
  }
  return 0;
}

BOOL bridge_virtual_node_commandhandler(ts_param_t *p,
                                        BYTE *__pCmd,
                                        BYTE cmdLength)
{
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *) __pCmd;

  switch (pCmd->ZW_Common.cmdClass) {
    default:
      CreateLogicalUDP(p, __pCmd, cmdLength);
      break;
  }

  return TRUE;
}

void temp_assoc_virtual_get_from_controller(nodeid_t newID);
void temp_assoc_virtual_node_clean(void);
BOOL is_virtual_node(nodeid_t nid);
/* Documented in sl_bridge.h */
void copy_virtual_nodes_mask_from_controller()
{
  memset(virtual_nodes_mask, 0, sizeof(virtual_nodes_mask));
  ZW_GetVirtualNodes(virtual_nodes_mask);
  /* TODO: Delete all remaining virtual nodes that are not used in association_table */
  LOG_PRINTF("virtual_nodes_mask: ")
  sl_print_hex_to_string(virtual_nodes_mask, 3);
  LOG_PRINTF("\n");

  temp_assoc_virtual_node_clean();
  for (int i = 1; i <= 100; i++) {
    if (i == MyNodeID) {
      continue;
    }

    if (NODEMASK_TEST_NODE(i, virtual_nodes_mask)) {
      if (is_virtual_node(i)) {
        LOG_PRINTF("get virtual node: %d\n", i);
        temp_assoc_virtual_get_from_controller(i);
      }
    }
  }
}

/* Documented in sl_bridge.h */
BOOL is_virtual_node(nodeid_t nid)
{
  /**
   * LR virtual nodes are always 4002 - 4005 and won't be shown up in virtual_node_list
   * so we have to do special handling here. As the plan is to abandon the virtual nodes
   * feature, this hack for the moment is acceptable.
   */
  if ((is_classic_node(nid) && BIT8_TST(nid - 1, virtual_nodes_mask))
      || ((nid <= 4005) && (nid >= 4002))) {
    return true;
  }
  return false;
}

void bridge_init()
{
  uint8_t cap;

  const APPL_NODE_TYPE virtual_node_type = { GENERIC_TYPE_REPEATER_SLAVE,
                                             SPECIFIC_TYPE_VIRTUAL_NODE };
  const uint8_t virtual_nop_classes[] = { COMMAND_CLASS_NO_OPERATION };

  ip_assoc_init();
  temp_assoc_init();

  bridge_state = booting;

  SerialAPI_ApplicationSlaveNodeInformation(0,
                                            1,
                                            virtual_node_type,
                                            virtual_nop_classes,
                                            sizeof(virtual_nop_classes));

  ip_assoc_unpersist_association_table();
  temp_assoc_unpersist_virtual_nodeids();

  print_bridge_status();

  cap = ZW_GetControllerCapabilities();

  if ((cap & CONTROLLER_NODEID_SERVER_PRESENT)
      || (cap & CONTROLLER_IS_SECONDARY) == 0) {
    // Doing add node stop before virtual node creation
    // to avoid race condition after setdefault.
    // Otherwise we would enable Smart Start lean mode before creating
    // virtual nodes, and that would disrupt the virtual node creation.
    ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);

    /* Start adding the virtual nodes preallocated for temporary associations */
    temp_assoc_delayed_add_virtual_nodes();
  } else {
    ERR_PRINTF("Gateway is included into a NON SIS network\n");
    ERR_PRINTF("there will be limited functionality available\n");
    bridge_state = initfail;
    zw_zip_post_event(ZIP_EVENT_BRIDGE_INITIALIZED, 0);
  }
}

/**
 * Clear and persist the (empty) association tables
 */
static void bridge_clear_association_tables(void)
{
  ip_assoc_init();
  ip_assoc_persist_association_table();

  temp_assoc_init();
  temp_assoc_persist_virtual_nodeids();
}

void resume_bridge_init(void)
{
  temp_assoc_resume_init();
}

void bridge_reset()
{
  bridge_clear_association_tables();

  bridge_state = booting;
}

bool bridge_idle(void)
{
  return ((bridge_state == initialized)
          && (is_assoc_create_in_progress() == FALSE));
}

/* Documented in sl_bridge.h */
void print_association_list_line(uint8_t line_no,
                                 uint8_t resource_endpoint,
                                 uint16_t resource_port,
                                 uip_ip6addr_t *resource_ip,
                                 nodeid_t virtual_id,
                                 uint8_t virtual_endpoint,
                                 nodeid_t han_nodeid,
                                 uint8_t han_endpoint,
                                 const char *type_str)
{
  LOG_PRINTF("#%2d: [%-5s] Virtual: %d.%d - HAN: %d.%d\n",
             line_no,
             type_str,
             virtual_id,
             virtual_endpoint,
             han_nodeid,
             han_endpoint);

  LOG_PRINTF(" LAN endpoint: %d port: %d IP: ",
             resource_endpoint,
             resource_port);

  uip_debug_ipaddr_print(resource_ip);
}

/* Documented in sl_bridge.h */

void print_bridge_associations(void)
{
  ip_assoc_print_association_table();
  temp_assoc_print_association_table();
}

void print_bridge_status(void)
{
  LOG_PRINTF("bridge_state: %u\n", bridge_state);

  print_bridge_associations();

  LOG_PRINTF("\n");

  temp_assoc_print_virtual_node_ids();
}
