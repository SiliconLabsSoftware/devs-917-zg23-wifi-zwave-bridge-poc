/*******************************************************************************
 * @file  sl_bridge_ip_assoc.h
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

#ifndef BRIDGE_IP_ASSOC_H_
#define BRIDGE_IP_ASSOC_H_

/**
 * \addtogroup ip_emulation
 * @{
 */

/**
 * Maximum number of associations created by IP Association command class.
 * Since IP associations are persisted to the EEPROM file, great care should be
 * taken before this number is modified.
 */
#define MAX_IP_ASSOCIATIONS              10

#include "stdint.h"
#include "sl_rd_types.h"
#include "sl_uip_def.h"
#include "ZW_typedefs.h"
#include "ZW_udp_server.h"
#include "modules/sl_rd_data_store.h"

typedef enum {BRIDGE_FAIL, BRIDGE_OK} bridge_return_status_t;

/**
 * Callback function type taking a ip_association_t pointer as parameter.
 */
typedef void (*ip_assoc_cb_func_t)(ip_association_t *);

void ip_assoc_init(void);

/**
 * Save the IP associations table to persistent storage.
 */
void ip_assoc_persist_association_table(void);

/**
 * Load the IP association table from the persistent storage.
 */
void ip_assoc_unpersist_association_table(void);

/**
 * Command handler for Z/IP IP Association commands.
 *
 * @param c Z/IP connection object
 * @param payload Payload of Z/IP package
 * @param len Length of payload (in bytes)
 * @param was_dtls Was IP frame received via DTLS?
 * @return TRUE if the command was handled, FALSE otherwise
 */
BOOL handle_ip_association(zwave_connection_t* c,
                           const u8_t* payload,
                           u8_t len,
                           BOOL was_dtls);

BOOL is_ip_assoc_create_in_progress(void);

void ip_assoc_remove_by_nodeid(nodeid_t node_id);

/**
 * Locate an IP association by its virtual node ID.
 *
 * @param virtnode Virtual node ID
 * @return The IP association for virtual node virtnode
 * @return NULL if no IP association exist for virtnode
 */
ip_association_t * ip_assoc_lookup_by_virtual_node(nodeid_t virtnode);

void ip_assoc_print_association_table(void);

/**
 * @}
 */

#endif // BRIDGE_IP_ASSOC_H_
