/*******************************************************************************
 * @file  sl_bridge.h
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
#ifndef BRIDGE_H_
#define BRIDGE_H_
#include "ZW_udp_server.h"
#include "ZW_classcmd.h"
#include "ZW_zip_classcmd.h"
#include "ZW_classcmd.h" /* ZW_APPLICATION_TX_BUFFER, etc */
#include "sl_rd_types.h"
#include <stdbool.h>

/**
 * \ingroup ip_emulation
 *
 * @{
 */

typedef enum {
  booting,
  initialized,
  initfail,
} bridge_state_t;

extern bridge_state_t bridge_state;

extern uint8_t virtual_nodes_mask[MAX_CLASSIC_NODEMASK_LENGTH];

/**
 *  Initialize the bridge.
 */
void bridge_init(void);

void resume_bridge_init(void);

/**
 * Invalidate bridge.
 *
 * Clear association table and set state to #booting.
 */
void bridge_reset(void);

/** Check if the bridge module is idle.
 *
 * Idle means the bridge is correctly initialized and no associations
 * are currently being created.
 */
bool bridge_idle(void);

void ApplicationCommandHandler_Bridge(BYTE rxStatus,
                                      nodeid_t destNode,
                                      nodeid_t sourceNode,
                                      ZW_APPLICATION_TX_BUFFER *pCmd,
                                      BYTE cmdLength);

/**
 * Check if an IP or temporary association creation is in progress.
 *
 * One of the checked callback function pointers are set when an association is
 * being created and cleared when done.
 *
 * @return TRUE an association is currently being created, FALSE otherwise.
 */
BOOL is_assoc_create_in_progress(void);

/**
 * Query if a node is virtual.
 *
 * Checks if the provided node ID is registered as a virtual node.
 *
 * \note Uses an in-memory nodemask of virtual node. That nodemask is synced
 * from the controller when calling copy_virtual_nodes_mask_from_controller().
 * In other words, that function must have been called prior to calling is_virtual_node().
 *
 * @param nid Node ID to check
 * @return TRUE is nid is a virtual node. FALSE otherwise.
 */
BOOL is_virtual_node(nodeid_t nid);

/**
 * Update in-memory copy of virtual nodes mask from controller.
 */
void copy_virtual_nodes_mask_from_controller(void);

/**
 * Return true if a matching virtual node session was found.
 */
BOOL bridge_virtual_node_commandhandler(
  ts_param_t* p,
  BYTE *pCmd,
  BYTE cmdLength);

/**
 *  Print a list of all active bridge associations to console.
 */
void print_bridge_associations(void);

/**
 *   Print bridge status to console.
 */
void print_bridge_status(void);

/**
 * Print formatted line with association properties.
 *
 * Helper for print_bridge_associations().
 */
void
print_association_list_line(uint8_t line_no,
                            uint8_t resource_endpoint,
                            uint16_t resource_port,
                            uip_ip6addr_t *resource_ip,
                            nodeid_t virtual_id,
                            uint8_t virtual_endpoint,
                            nodeid_t han_nodeid,
                            uint8_t han_endpoint,
                            const char *type_str);

/**
 * @}
 */
#endif /* BRIDGE_H_ */
