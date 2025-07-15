/***************************************************************************//**
 * @file  sl_rd_data_store.c
 * @brief Store resource directory data in NVM3
 *******************************************************************************
 * # License
 * <b>Copyright 2025 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * SPDX-License-Identifier: LicenseRef-MSLA
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of the Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement
 * By installing, copying or otherwise using this software, you agree to the
 * terms of the MSLA.
 *
 ******************************************************************************/

#include "stdio.h"
#include "sl_rd_data_store.h"
#include "sl_status.h"
#include "nvm3_default.h"
#include "sl_common_type.h"
#include "Common/sl_common_log.h"
#include "lib/memb.h"
#include "stddef.h"
#include "string.h"
#include "sl_gw_info.h"

// Below are depends from "sl_bridge_ip_assoc.h" and "zip_router_config.h, remove comments to build

#define MAX_IP_ASSOCIATIONS              35

typedef struct  _Gw_Config_St_{
  u8_t mode;
  u8_t showlock;
  u8_t peerProfile;
  u8_t actualPeers;
}Gw_Config_St_t;

typedef struct  _Gw_PeerProfile_St_{
  uip_ip6addr_t   peer_ipv6_addr;
  u8_t      port1;
  u8_t      port2;
  u8_t        peerNameLength;
  char        peerName[63];
}Gw_PeerProfile_St_t;

/****************************************************************************/
/*                            DEFINES                                       */
/****************************************************************************/

#define DATA_BASE_SCHEMA_VERSION_MAJOR 1
#define DATA_BASE_SCHEMA_VERSION_MINOR 0

#define MAX_NODE                          40
#define MAX_ENDPOINT_PER_NODE             4
#define MAX_ENDPOINT                      (MAX_ENDPOINT_PER_NODE * MAX_NODE) // 4 * 40 = 160

// Node data key
#define NODE_NAME_DATA_KEY_OFFSET         MAX_NODE // 40
#define MAX_NODE_NAME_DATA_KEY_OFFSET     (NODE_NAME_DATA_KEY_OFFSET + MAX_NODE) // 40 + 40 = 80

#define DSK_DATA_KEY_OFFSET               MAX_NODE_NAME_DATA_KEY_OFFSET // 80
#define MAX_DSK_DATA_KEY_OFFSET           (DSK_DATA_KEY_OFFSET + MAX_NODE) // 80 + 40 = 120

#define NODE_CC_VERSIONS_OFFSET           MAX_DSK_DATA_KEY_OFFSET // 120
#define MAX_NODE_CC_VERSIONS_OFFSET       (NODE_CC_VERSIONS_OFFSET + MAX_NODE) // 120 + 40 = 160

// Endpoint data key
#define ENDPOINT_DATA_KEY_OFFSET          MAX_NODE_CC_VERSIONS_OFFSET // 160
#define MAX_ENDPOINT_DATA_KEY_OFFSET      (ENDPOINT_DATA_KEY_OFFSET + MAX_ENDPOINT) // 160 + 160 = 320
#define ENDPOINT_INFO_KEY_OFFSET          MAX_ENDPOINT_DATA_KEY_OFFSET // 320
#define MAX_ENDPOINT_INFO_KEY_OFFSET      (ENDPOINT_INFO_KEY_OFFSET + MAX_ENDPOINT) // 320 + 160 = 480
#define ENDPOINT_AGG_KEY_OFFSET           MAX_ENDPOINT_INFO_KEY_OFFSET // 480
#define MAX_ENDPOINT_AGG_KEY_OFFSET       (ENDPOINT_AGG_KEY_OFFSET + MAX_ENDPOINT) // 480 + 160 = 640
#define ENDPOINT_NAME_KEY_OFFSET          MAX_ENDPOINT_AGG_KEY_OFFSET // 640
#define MAX_ENDPOINT_NAME_KEY_OFFSET      (ENDPOINT_NAME_KEY_OFFSET + MAX_ENDPOINT) // 640 + 160 = 800
#define ENDPOINT_LOCATION_KEY_OFFSET      MAX_ENDPOINT_NAME_KEY_OFFSET // 800
#define MAX_ENDPOINT_LOCATION_KEY_OFFSET  (ENDPOINT_LOCATION_KEY_OFFSET + MAX_ENDPOINT) // 800 + 160 = 960

// IP association key
#define IP_ASSOCIATION_OFFSET             MAX_ENDPOINT_LOCATION_KEY_OFFSET // 960
#define MAX_IP_ASSOCIATION_OFFSET         (IP_ASSOCIATION_OFFSET + MAX_IP_ASSOCIATIONS) // 960 + 200 = 1160

// Virtual node key
#define VIRTUAL_NODE_KEY_OFFSET           MAX_IP_ASSOCIATION_OFFSET // 1160
#define MAX_VIRTUAL_NODE                  10
#define MAX_VIRTUAL_NODE_KEY_OFFSET       (VIRTUAL_NODE_KEY_OFFSET + MAX_VIRTUAL_NODE) // 1160 + 40 = 1200

// Network key
#define NETWORK_KEY_OFFSET                MAX_VIRTUAL_NODE_KEY_OFFSET // 1200

// Gateway config key
#define GW_CONFIG_KEY_OFFSET              NETWORK_KEY_OFFSET + 1  // 1201

// Peer profile key
#define PEER_PROFILE_KEY_OFFSET           GW_CONFIG_KEY_OFFSET + 1  // 1202
#define NUMBER_OF_PEER_PROFILE            10
#define MAX_PEER_PROFILE_KEY_OFFSET      (PEER_PROFILE_KEY_OFFSET + NUMBER_OF_PEER_PROFILE) // 1202 + 10 = 1212

/****************************************************************************/
/*                            LOCAL VARIABLES                               */
/****************************************************************************/

/**
 * Network information
 */
typedef struct rd_network_database_entry {
  uint32_t homeID;
  uint16_t nodeID;
  uint8_t version_major;
  uint8_t version_minor;
} rd_network_database_entry_t;

uint16_t sli_endpoint_key_counter = 0;
uint8_t sli_ip_association_key_counter = 0;

/****************************************************************************/
/*                            PRIVATE FUNCTIONS                             */
/****************************************************************************/

void sli_free_node_data_memory(rd_node_database_entry_t *n)
{
  free(n);
}

sl_status_t sli_pointer_data_to_nvm3(uint16_t offset_key, void* data, size_t dataLen)
{
  sl_status_t status = SL_STATUS_FAIL;

  status = nvm3_writeData(nvm3_defaultHandle, offset_key,
                          data, dataLen);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Save pointer to nvm3 fail: %ld", status);
  }
  return status;
}

static void clear_database()
{
  sl_status_t status;
  rd_network_database_entry_t network_database;

  LOG_PRINTF("Clearing database HomeID=%08lX nodeID=%i\n", homeID, MyNodeID);
  rd_data_store_invalidate();
  //Initialize database with network info and database schema version.
  network_database.homeID = homeID;
  network_database.nodeID = MyNodeID;
  network_database.version_major = DATA_BASE_SCHEMA_VERSION_MAJOR;
  network_database.version_minor = DATA_BASE_SCHEMA_VERSION_MINOR;
  status = nvm3_writeData(nvm3_defaultHandle, NETWORK_KEY_OFFSET,
                          &network_database, sizeof(rd_network_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to init network info: %ld\n", status);
    return;
  }
}

/****************************************************************************/
/*                            PUBLIC FUNCTIONS                              */
/****************************************************************************/
static char ds_is_inited = 0;
/**
 * @brief Initialize the data store and load network information from NVM3.
 *
 * Initializes the NVM3 storage and checks for existing network information.
 * If the stored network information does not match the current network, the database is cleared.
 *
 * @return true on success, false on failure.
 */
bool data_store_init(void)
{
  sl_status_t status;
  rd_network_database_entry_t network_database;

  if (ds_is_inited == 1) {
    return true;
  }
  ds_is_inited = 1;

  status = nvm3_initDefault();
  LOG_PRINTF("\r\n NVM3 init status %lx \r\n", status);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to initialize NVM3: %ld\n", status);
    return false;
  }

  // Check network info store in DB vs current network info
  status = nvm3_readData(nvm3_defaultHandle, NETWORK_KEY_OFFSET,
                         &network_database, sizeof(rd_network_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read network data from nvm3: %ld\n", status);
    clear_database();
  } else {
    if (network_database.homeID != homeID || network_database.nodeID != MyNodeID) {
      LOG_PRINTF("Network data mismatch, clearing database.\n");
      clear_database();
    }
  }

  return true;
}

/**
 * @brief Deinitialize the data store and NVM3.
 *
 * Calls the NVM3 deinitialization routine to release resources.
 */
void data_store_exit(void)
{
  nvm3_deinitDefault();
}

/***************************** Node database **********************/

static bool read_endpoint_from_nvm3(int i, rd_ep_database_entry_t *e, nodeid_t nodeid, rd_node_database_entry_t *n)
{
  sl_status_t status;

  status = nvm3_readData(nvm3_defaultHandle, i, e, sizeof(rd_ep_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Endpoint ID %i not found, nvm3 read error: %ld\n", i, status);
    return false;
  }
  if (e->nodeID != nodeid) {
    return false;
  }

  e->endpoint_name = rd_data_mem_alloc(e->endpoint_name_len);
  e->endpoint_location = rd_data_mem_alloc(e->endpoint_loc_len);
  e->endpoint_agg = rd_data_mem_alloc(e->endpoint_aggr_len);
  e->endpoint_info = rd_data_mem_alloc(e->endpoint_info_len);
  if ((!e->endpoint_name) || (!e->endpoint_location)
      || (!e->endpoint_agg) || (!e->endpoint_info)) {
    LOG_PRINTF("Out of memory\n");
    return false;
  }

  status = nvm3_readData(nvm3_defaultHandle, i + ENDPOINT_NAME_KEY_OFFSET,
                         e->endpoint_name, e->endpoint_name_len);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read endpoint name from NVM3: %ld\n", status);
    return false;
  }

  status = nvm3_readData(nvm3_defaultHandle, i + ENDPOINT_LOCATION_KEY_OFFSET,
                         e->endpoint_location, e->endpoint_loc_len);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read endpoint location from NVM3: %ld\n", status);
    return false;
  }

  status = nvm3_readData(nvm3_defaultHandle, i + ENDPOINT_AGG_KEY_OFFSET,
                         e->endpoint_agg, e->endpoint_aggr_len);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read endpoint agg from NVM3: %ld\n", status);
    return false;
  }

  status = nvm3_readData(nvm3_defaultHandle, i + ENDPOINT_INFO_KEY_OFFSET,
                         e->endpoint_info, e->endpoint_info_len);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read endpoint info from NVM3: %ld\n", status);
    return false;
  }

  list_add(n->endpoints, e);
  n->nEndpoints++;
  return true;
}

rd_node_database_entry_t* rd_data_store_read(nodeid_t nodeID)
{
  sl_status_t status;
  rd_node_database_entry_t *n = NULL;

  n = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
  if (!n) {
    LOG_PRINTF("Out of memory\n");
    return NULL;
  }

  // Get node database entry data
  status = nvm3_readData(nvm3_defaultHandle, nodeID,
                         n, sizeof(rd_node_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Node ID %i not found\n", nodeID);
    sli_free_node_data_memory(n);
    return NULL;
  }
  if (n->nodeid != nodeID) {
    LOG_PRINTF("Node ID mismatch\n");
    sli_free_node_data_memory(n);
    return NULL;
  }

  // Get nodename, dsk, node_cc_versions in node database
  n->nodename = rd_data_mem_alloc(n->nodeNameLen);
  n->dsk = rd_data_mem_alloc(n->dskLen);
  n->node_cc_versions = rd_data_mem_alloc(n->node_cc_versions_len);

  if ((!n->nodename) || (!n->dsk) || (!n->node_cc_versions)) {
    LOG_PRINTF("Out of memory\n");
    rd_data_store_mem_free(n);
    return NULL;
  }

  status = nvm3_readData(nvm3_defaultHandle, n->nodeid + NODE_NAME_DATA_KEY_OFFSET,
                         n->nodename, n->nodeNameLen);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read node name from nvm3: %ld\n", status);
    rd_data_store_mem_free(n);
    return NULL;
  }
  status = nvm3_readData(nvm3_defaultHandle, n->nodeid + DSK_DATA_KEY_OFFSET,
                         n->dsk, n->dskLen);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read dsk from nvm3: %ld\n", status);
    rd_data_store_mem_free(n);
    return NULL;
  }
  status = nvm3_readData(nvm3_defaultHandle, n->nodeid + NODE_CC_VERSIONS_OFFSET,
                         n->node_cc_versions, n->node_cc_versions_len);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read node cc versions from nvm3: %ld\n", status);
    rd_data_store_mem_free(n);
    return NULL;
  }

  // Get endpoint database entry data
  LIST_STRUCT_INIT(n, endpoints);
  n->pcvs = NULL;
  n->nEndpoints = 0;
  n->refCnt = 0;

  for (int i = ENDPOINT_DATA_KEY_OFFSET; i < MAX_ENDPOINT_DATA_KEY_OFFSET; i++) {
    rd_ep_database_entry_t *e = rd_data_mem_alloc(sizeof(rd_ep_database_entry_t));
    if (!e) {
      LOG_PRINTF("Out of memory\n");
      rd_data_store_mem_free(n);
      return NULL;
    }
    if (!read_endpoint_from_nvm3(i, e, n->nodeid, n)) {
      rd_store_mem_free_ep(e);
      continue;
    }
  }

  return n;
}

/**
 * @brief Write a node database entry and its endpoints to NVM3.
 *
 * Persists the given node database entry and all associated endpoint data to NVM3.
 *
 * @param n Pointer to the node database entry to write.
 */
void rd_data_store_nvm_write(rd_node_database_entry_t *n)
{
  sl_status_t status;
  rd_ep_database_entry_t *e;
  // Save rd_node_database_entry_t to nvm3
  status = nvm3_writeData(nvm3_defaultHandle, n->nodeid,
                          (rd_node_database_entry_t *)n,
                          sizeof(rd_node_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Write data to nvm3 fail: %ld\n\r", status);
    return;
  }

  // Save nodename, dsk, node_cc_versions to nvm3
  status = sli_pointer_data_to_nvm3(n->nodeid + NODE_NAME_DATA_KEY_OFFSET,
                                    (uint8_t *)n->nodename, n->nodeNameLen);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Write node name to nvm3 fail: %ld\n\r", status);
    return;
  }

  status = sli_pointer_data_to_nvm3(n->nodeid + DSK_DATA_KEY_OFFSET,
                                    (uint8_t *)n->dsk, n->dskLen);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Write dsk to nvm3 fail: %ld\n\r", status);
    return;
  }

  status = sli_pointer_data_to_nvm3(n->nodeid + NODE_CC_VERSIONS_OFFSET,
                                    (cc_version_pair_t *)n->node_cc_versions,
                                    n->node_cc_versions_len);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Write node cc version to nvm3 fail: %ld\n\r", status);
    return;
  }

  // Save endpoint to nvm3
  for (e = list_head(n->endpoints); e; e = list_item_next(e)) {
    e->nodeID = n->nodeid;
    status = nvm3_writeData(nvm3_defaultHandle,
                            sli_endpoint_key_counter + ENDPOINT_DATA_KEY_OFFSET,
                            e, sizeof(rd_ep_database_entry_t));
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Write endpoint database entry to nvm3 fail: %ld\n\r", status);
      return;
    }

    status = sli_pointer_data_to_nvm3(sli_endpoint_key_counter + ENDPOINT_INFO_KEY_OFFSET,
                                      e->endpoint_info, e->endpoint_info_len);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Write endpoint info to nvm3 fail: %ld\n\r", status);
      return;
    }
    status = sli_pointer_data_to_nvm3(sli_endpoint_key_counter + ENDPOINT_AGG_KEY_OFFSET,
                                      e->endpoint_agg, e->endpoint_aggr_len);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Write endpoint agg to nvm3 fail: %ld\n\r", status);
      return;
    }
    status = sli_pointer_data_to_nvm3(sli_endpoint_key_counter + ENDPOINT_LOCATION_KEY_OFFSET,
                                      e->endpoint_location, e->endpoint_loc_len);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Write endpoint location to nvm3 fail: %ld\n\r", status);
      return;
    }
    status = sli_pointer_data_to_nvm3(sli_endpoint_key_counter + ENDPOINT_NAME_KEY_OFFSET,
                                      e->endpoint_name, e->endpoint_name_len);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Write endpoint name to nvm3 fail: %ld\n\r", status);
      return;
    }
    sli_endpoint_key_counter++;
  }
}

/**
 * @brief Delete a node database entry and its endpoints from NVM3.
 *
 * Removes the node entry and all associated endpoint entries from NVM3.
 *
 * @param n Pointer to the node database entry to delete.
 */
void rd_data_store_nvm_free(rd_node_database_entry_t *n)
{
  sl_status_t status;
  rd_ep_database_entry_t e;

  // Delete node entry
  status = nvm3_deleteObject(nvm3_defaultHandle, n->nodeid);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Fail to delete a node database entry in db: %ld\n", status);
    // no node in db, just return
    return;
  }

  // Delete endpoint entry
  for (int i = ENDPOINT_DATA_KEY_OFFSET; i < MAX_ENDPOINT_DATA_KEY_OFFSET; i++) {
    nvm3_readData(nvm3_defaultHandle, i, &e, sizeof(rd_ep_database_entry_t));
    if (e.nodeID == n->nodeid) {
      status = nvm3_deleteObject(nvm3_defaultHandle, i);
      if (status != SL_STATUS_OK) {
        LOG_PRINTF("Fail to delete endpoint database entry %d in db", e.nodeID);
      }
    }
  }
}

/**
 * @brief Update a node database entry in NVM3.
 *
 * (Currently not implemented; placeholder for future update logic.)
 *
 * @param n Pointer to the node database entry to update.
 */
void rd_data_store_update(rd_node_database_entry_t *n)
{
  (void)n;
}

/****************** IP associations **********************/

void rd_data_store_persist_associations(list_t ip_association_table)
{
  sl_status_t status;

  // Clear the old table
  for (int i = IP_ASSOCIATION_OFFSET; i < MAX_IP_ASSOCIATION_OFFSET; i++) {
    status = nvm3_deleteObject(nvm3_defaultHandle, i);
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Failed to delete ip table\n");
      return;
    }
    sli_ip_association_key_counter = 0;
  }

  for (ip_association_t *a = list_head(ip_association_table); a != NULL; a = list_item_next(a)) {
    if (sli_ip_association_key_counter >= MAX_IP_ASSOCIATIONS) {
      LOG_PRINTF("Too many ip associations to store\n");
      return;
    }
    status = nvm3_writeData(nvm3_defaultHandle,
                            sli_ip_association_key_counter + IP_ASSOCIATION_OFFSET,
                            a, sizeof(ip_association_t));
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Failed to store ip association at key %d: %ld\n",
                 sli_ip_association_key_counter + IP_ASSOCIATION_OFFSET, status);
      return;
    }
    sli_ip_association_key_counter++;
  }
}

void rd_datastore_unpersist_association(list_t ip_association_table, struct memb *ip_association_pool)
{
  sl_status_t status;

  for (int i = IP_ASSOCIATION_OFFSET;
       i < (IP_ASSOCIATION_OFFSET + sli_ip_association_key_counter); i++) {
    ip_association_t *a = (ip_association_t *)memb_alloc(ip_association_pool);
    if (!a) {
      LOG_PRINTF("Out of memory during unpersist of association table\n");
      return;
    }

    status = nvm3_readData(nvm3_defaultHandle, i, a, sizeof(ip_association_t));
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Failed to unpersist association: %ld", status);
      memb_free(ip_association_pool, a);
    } else {
      list_add(ip_association_table, a);
    }
  }
}

void rd_datastore_persist_virtual_nodes(const nodeid_t *nodelist, size_t node_count)
{
  sl_status_t status;

  if (node_count > MAX_VIRTUAL_NODE) {
    LOG_PRINTF("Too many virtual nodes to store, need to increase MAX_VIRTUAL_NODE\n");
    return;
  }

  // Save new virtual node table
  for (int i = VIRTUAL_NODE_KEY_OFFSET; i < (int)(VIRTUAL_NODE_KEY_OFFSET + node_count); i++) {
    status = nvm3_writeData(nvm3_defaultHandle, i, &nodelist[i], sizeof(nodeid_t));
    if (status != SL_STATUS_OK) {
      LOG_PRINTF("Failed to store virtual node at key %d: %ld\n", i, status);
      break;
    }
  }
}

size_t rd_datastore_unpersist_virtual_nodes(nodeid_t *nodelist, size_t max_node_count)
{
  (void)nodelist; // Đánh dấu tham số không sử dụng để tránh lỗi biên dịch
  size_t node_count = 0;

  if (max_node_count > MAX_VIRTUAL_NODE) {
    LOG_PRINTF("Too many virtual nodes to read, need to increase MAX_VIRTUAL_NODE\n");
    return 0;
  }
  LOG_PRINTF("Dont support store v-nodes\n");

  return node_count;
}

/**************************** Generic stuff *************************/
void rd_data_store_invalidate()
{
  // Clear nodes, endpoints, network, ip associations.
  for (int i = 0; i < MAX_NODE; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }
  for (int i = NODE_NAME_DATA_KEY_OFFSET; i < MAX_NODE_NAME_DATA_KEY_OFFSET; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }
  for (int i = DSK_DATA_KEY_OFFSET; i < MAX_DSK_DATA_KEY_OFFSET; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }
  for (int i = NODE_CC_VERSIONS_OFFSET; i < MAX_NODE_CC_VERSIONS_OFFSET; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }

  for (int i = ENDPOINT_DATA_KEY_OFFSET; i < MAX_ENDPOINT_DATA_KEY_OFFSET; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }
  for (int i = ENDPOINT_INFO_KEY_OFFSET; i < MAX_ENDPOINT_DATA_KEY_OFFSET; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }
  for (int i = ENDPOINT_AGG_KEY_OFFSET; i < MAX_ENDPOINT_DATA_KEY_OFFSET; i++) {
    nvm3_deleteObject(nvm3_defaultHandle, i);
  }

  sli_endpoint_key_counter = 0;
}

void rd_data_store_version_get(uint8_t *major, uint8_t *minor)
{
  sl_status_t status;
  rd_network_database_entry_t network_database;

  status = nvm3_readData(nvm3_defaultHandle, NETWORK_KEY_OFFSET,
                         &network_database, sizeof(rd_network_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read network data from nvm3: %ld\n", status);
    *major = 0;
    *minor = 0;
    return;
  }

  *major = network_database.version_major;
  *minor = network_database.version_minor;
}

/********************* Standard memory allocations *************************/

void rd_data_store_mem_free(rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *ep;

  while ((ep = list_pop(n->endpoints))) {
    rd_store_mem_free_ep(ep);
  }

  if (n->nodename) {
    rd_data_mem_free(n->nodename);
  }
  if (n->dsk) {
    rd_data_mem_free(n->dsk);
  }
  if (n->node_cc_versions) {
    rd_data_mem_free(n->node_cc_versions);
  }
  /* pcvs is not persisted in eeprom */
  if (n->pcvs) {
    free(n->pcvs);
  }

  rd_data_mem_free(n);
}

void rd_store_mem_free_ep(rd_ep_database_entry_t *ep)
{
  if (ep->endpoint_info) {
    rd_data_mem_free(ep->endpoint_info);
  }
  if (ep->endpoint_name) {
    rd_data_mem_free(ep->endpoint_name);
  }
  if (ep->endpoint_location) {
    rd_data_mem_free(ep->endpoint_location);
  }
  rd_data_mem_free(ep);
}

void* rd_data_mem_alloc(uint8_t size)
{
  return malloc(size);
}

void rd_data_mem_free(void *p)
{
  if (p) {
    free(p);
  }
}

uint32_t rd_zgw_homeid_get()
{
  sl_status_t status;
  rd_network_database_entry_t network_database;

  status = nvm3_readData(nvm3_defaultHandle, NETWORK_KEY_OFFSET,
                         &network_database, sizeof(rd_network_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read homeid from nvm3: %ld\n", status);
    return 0;
  }
  return network_database.homeID;
}

nodeid_t rd_zgw_nodeid_get()
{
  sl_status_t status;
  rd_network_database_entry_t network_database;

  status = nvm3_readData(nvm3_defaultHandle, NETWORK_KEY_OFFSET,
                         &network_database, sizeof(rd_network_database_entry_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to read nodeid from nvm3: %ld\n", status);
    return 0;
  }

  return network_database.nodeID;
}

/**
 * @brief Persist the gateway configuration to NVM3.
 *
 * Stores the provided gateway configuration structure to NVM3.
 *
 * @param gw_cfg Pointer to the gateway configuration to persist.
 */
void rd_datastore_persist_gw_config(const Gw_Config_St_t *gw_cfg)
{
  sl_status_t status;

  // Clear the old table
  status = nvm3_deleteObject(nvm3_defaultHandle, GW_CONFIG_KEY_OFFSET);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to delete gateway config\n");
    return;
  }

  status = nvm3_writeData(nvm3_defaultHandle, GW_CONFIG_KEY_OFFSET,
                          gw_cfg, sizeof(Gw_Config_St_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to store gateway config: %ld\n", status);
  }
}

/**
 * @brief Restore the gateway configuration from NVM3.
 *
 * Reads the gateway configuration from NVM3 into the provided structure.
 *
 * @param gw_cfg Pointer to the gateway configuration structure to populate.
 */
void rd_datastore_unpersist_gw_config(Gw_Config_St_t *gw_cfg)
{
  sl_status_t status;
  Gw_Config_St_t read_gw_config;

  status = nvm3_readData(nvm3_defaultHandle, GW_CONFIG_KEY_OFFSET,
                         &read_gw_config, sizeof(Gw_Config_St_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to unpersist gateway config: %ld\n", status);
    memset(gw_cfg, 0, sizeof(Gw_Config_St_t));
  }
  gw_cfg->mode = read_gw_config.mode;
  gw_cfg->showlock = read_gw_config.showlock;
  gw_cfg->peerProfile = read_gw_config.peerProfile;
  gw_cfg->actualPeers = read_gw_config.actualPeers;
}

/**
 * @brief Persist a peer profile to NVM3.
 *
 * Stores the provided peer profile at the specified index in NVM3.
 *
 * @param index   Index at which to store the profile.
 * @param profile Pointer to the peer profile to persist.
 */
void rd_datastore_persist_peer_profile(int index, const Gw_PeerProfile_St_t *profile)
{
  sl_status_t status;

  // Clear the old peer
  status = nvm3_deleteObject(nvm3_defaultHandle, (PEER_PROFILE_KEY_OFFSET + index));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to delete peer profile\n");
    return;
  }

  status = nvm3_writeData(nvm3_defaultHandle, (PEER_PROFILE_KEY_OFFSET + index),
                          profile, sizeof(Gw_PeerProfile_St_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to store peer profile: %ld\n", status);
  }
}

/**
 * @brief Restore a peer profile from NVM3.
 *
 * Reads the peer profile at the specified index from NVM3 into the provided structure.
 *
 * @param index   Index of the profile to restore.
 * @param profile Pointer to the peer profile structure to populate.
 */
void rd_datastore_unpersist_peer_profile(int index, Gw_PeerProfile_St_t *profile)
{
  sl_status_t status;

  status = nvm3_readData(nvm3_defaultHandle, (PEER_PROFILE_KEY_OFFSET + index),
                         profile, sizeof(Gw_PeerProfile_St_t));
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to unpersist peer profile: %ld\n", status);
    memset(profile, 0, sizeof(Gw_PeerProfile_St_t));
  }
}
