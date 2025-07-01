/***************************************************************************/ /**
 * @file RD_internal.c
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
#include <rsi_ble_apis.h>
#include <rsi_bt_common_apis.h>
#include <rsi_common_apis.h>
#include "ZW_classcmd_ex.h"
#include "RD_internal.h"
#include "modules/sl_rd_data_store.h"
#include "sl_common_type.h"
#include "sl_common_log.h"
#include "ZW_transport_api.h"
#include "utls/zgw_nodemask.h"
// #include "zgw_str.h"
#include "zwdb.h"

#include <string.h>
#include <strings.h>
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>

#include "sl_uip_def.h"
#include "zw_network_info.h" /* MyNodeID */

#define MODE_FLAGS_DELETED 0x01
#define MODE_FLAGS_FAILED  0x02
#define MODE_FLAGS_LOWBAT  0x04

/* Consider to move this to a GW component, ref: ZGW-1243 */
const cc_version_pair_t controlled_cc_v[] = { { COMMAND_CLASS_VERSION, 0x0 },
                                              { COMMAND_CLASS_ZWAVEPLUS_INFO, 0x0 },
                                              { COMMAND_CLASS_MANUFACTURER_SPECIFIC, 0x0 },
                                              { COMMAND_CLASS_WAKE_UP, 0x0 },
                                              { COMMAND_CLASS_MULTI_CHANNEL_V4, 0x0 },
                                              { COMMAND_CLASS_ASSOCIATION, 0x0 },
                                              { COMMAND_CLASS_MULTI_CHANNEL_ASSOCIATION_V3, 0x0 },
                                              { 0xffff, 0xff } };
/* This eats memory ! */
/**
 * The node database.
 */
static rd_node_database_entry_t* ndb[ZW_MAX_NODES];

const char *ep_state_name(int state)
{
  static char str[25];
  switch (state) {
    case EP_STATE_PROBE_INFO: return "EP_STATE_PROBE_INFO";
    case EP_STATE_PROBE_SEC2_C2_INFO: return "EP_STATE_PROBE_SEC2_C2_INFO";
    case EP_STATE_PROBE_SEC2_C1_INFO: return "EP_STATE_PROBE_SEC2_C1_INFO";
    case EP_STATE_PROBE_SEC2_C0_INFO: return "EP_STATE_PROBE_SEC2_C0_INFO";
    case EP_STATE_PROBE_SEC0_INFO: return "EP_STATE_PROBE_SEC0_INFO";
    case EP_STATE_PROBE_VERSION: return "EP_STATE_PROBE_VERSION";
    case EP_STATE_PROBE_ZWAVE_PLUS: return "EP_STATE_PROBE_ZWAVE_PLUS";
    case EP_STATE_MDNS_PROBE: return "EP_STATE_MDNS_PROBE";
    case EP_STATE_MDNS_PROBE_IN_PROGRESS: return "EP_STATE_MDNS_PROBE_IN_PROGRESS";
    case EP_STATE_PROBE_DONE: return "EP_STATE_PROBE_DONE";
    case EP_STATE_PROBE_FAIL: return "EP_STATE_PROBE_FAIL";
    default:
      sprintf(str, "%d", state);
      return str;
  }
};

const char* rd_node_probe_state_name(int state)
{
  static char str[25];

  switch (state) {
    case STATUS_CREATED: return "STATUS_CREATED";
    case STATUS_PROBE_NODE_INFO: return "STATUS_PROBE_NODE_INFO";
    case STATUS_PROBE_PRODUCT_ID: return "STATUS_PROBE_PRODUCT_ID";
    case STATUS_ENUMERATE_ENDPOINTS: return "STATUS_ENUMERATE_ENDPOINTS";
    case STATUS_FIND_ENDPOINTS: return "STATUS_FIND_ENDPOINTS";
    case STATUS_CHECK_WU_CC_VERSION: return "STATUS_CHECK_WU_CC_VERSION";
    case STATUS_GET_WU_CAP: return "STATUS_GET_WU_CAP";
    case STATUS_SET_WAKE_UP_INTERVAL: return "STATUS_SET_WAKE_UP_INTERVAL";
    case STATUS_ASSIGN_RETURN_ROUTE: return "STATUS_ASSIGN_RETURN_ROUTE";
    case STATUS_PROBE_WAKE_UP_INTERVAL: return "STATUS_PROBE_WAKE_UP_INTERVAL";
    case STATUS_PROBE_ENDPOINTS: return "STATUS_PROBE_ENDPOINTS";
    case STATUS_MDNS_PROBE: return "STATUS_MDNS_PROBE";
    case STATUS_MDNS_EP_PROBE: return "STATUS_MDNS_EP_PROBE";
    case STATUS_DONE: return "STATUS_DONE";
    case STATUS_PROBE_FAIL: return "STATUS_PROBE_FAIL";
    case STATUS_FAILING: return "STATUS_FAILING";
    default:
      sprintf(str, "%d", state);
      return str;
  }
};

uint8_t controlled_cc_v_size()
{
  return sizeof(controlled_cc_v);
}

int rd_mem_cc_versions_set_default(uint8_t node_cc_versions_len,
                                   cc_version_pair_t *node_cc_versions)
{
  if (node_cc_versions_len < sizeof(controlled_cc_v)) {
    return 0;
  }
  if (node_cc_versions) {
    memcpy(node_cc_versions, controlled_cc_v, sizeof(controlled_cc_v));
  }
  return sizeof(controlled_cc_v);
}

void rd_node_cc_versions_set_default(rd_node_database_entry_t *n)
{
  if (!n) {
    return;
  }
  if (n->node_cc_versions) {
    memcpy(n->node_cc_versions, controlled_cc_v, n->node_cc_versions_len);
  } else {
    LOG_PRINTF("Node CC version set default failed.\n");
  }
}

uint8_t rd_node_cc_version_get(rd_node_database_entry_t *n, uint16_t command_class)
{
  int i, cnt;
  uint8_t version;

  if (!n) {
    return 0;
  }

  if (!n->node_cc_versions) {
    return 0;
  }

  if (n->mode & MODE_FLAGS_DELETED) {
    return 0;
  }

  version = 0;
  cnt = n->node_cc_versions_len / sizeof(cc_version_pair_t);

  for (i = 0; i < cnt; i++) {
    if (n->node_cc_versions[i].command_class == command_class) {
      version = n->node_cc_versions[i].version;
      break;
    }
  }
  return version;
}

void rd_node_cc_version_set(rd_node_database_entry_t *n, uint16_t command_class, uint8_t version)
{
  int i, cnt;

  if (!n) {
    return;
  }

  if (!n->node_cc_versions) {
    return;
  }

  if (n->mode & MODE_FLAGS_DELETED) {
    return;
  }
  cnt = n->node_cc_versions_len / sizeof(cc_version_pair_t);

  for (i = 0; i < cnt; i++) {
    if (n->node_cc_versions[i].command_class == command_class) {
      n->node_cc_versions[i].version = version;
      break;
    }
  }
}

rd_node_database_entry_t* rd_node_entry_alloc(nodeid_t nodeid)
{
  rd_node_database_entry_t* nd = rd_data_mem_alloc(sizeof(rd_node_database_entry_t));
  ndb[nodeid - 1] = nd;
  if (nd != NULL) {
    memset(nd, 0, sizeof(rd_node_database_entry_t));
    nd->nodeid = nodeid;

    /*When node name is 0, then we use the default names*/
    nd->nodeNameLen = 0;
    nd->nodename = NULL;
    nd->dskLen = 0;
    nd->dsk = NULL;
    nd->pcvs = NULL;
    nd->state = STATUS_CREATED;
    nd->mode = MODE_PROBING;
    nd->security_flags = 0;
    nd->wakeUp_interval = DEFAULT_WAKE_UP_INTERVAL;    //Default wakeup interval is 70 minutes
    nd->node_cc_versions_len = controlled_cc_v_size();
    nd->node_cc_versions = rd_data_mem_alloc(nd->node_cc_versions_len);
    rd_node_cc_versions_set_default(nd);
    nd->node_version_cap_and_zwave_sw = 0x00;
    nd->probe_flags = RD_NODE_PROBE_NEVER_STARTED;
    nd->node_is_zws_probed = 0x00;
    nd->node_properties_flags = 0x0000;

    LIST_STRUCT_INIT(nd, endpoints);
  }

  return nd;
}

rd_node_database_entry_t* rd_node_entry_import(nodeid_t nodeid)
{
  ndb[nodeid - 1] = rd_data_store_read(nodeid);
  return ndb[nodeid - 1];
}

void rd_node_entry_free(nodeid_t nodeid)
{
  rd_node_database_entry_t* nd = ndb[nodeid - 1];
  rd_data_store_mem_free(nd);
  ndb[nodeid - 1] = NULL;
}

rd_node_database_entry_t* rd_node_get_raw(nodeid_t nodeid)
{
  return ndb[nodeid - 1];
}

void
rd_destroy()
{
  nodeid_t i;
  rd_node_database_entry_t *n;
  for (i = 0; i < ZW_MAX_NODES; i++) {
    n = ndb[i];
    if (n) {
      rd_data_store_mem_free(n);
      ndb[i] = 0;
    }
  }
}

u8_t rd_node_exists(nodeid_t node)
{
  if (node > 0 && node <= ZW_MAX_NODES) {
    return (ndb[node - 1] != 0);
  }
  return false;
}

rd_ep_database_entry_t* rd_ep_first(nodeid_t node)
{
  nodeid_t i;

  /* special rule for node 0 to be valid for searching all nodes */
  if ((node > ZW_MAX_NODES)
      || ((node > ZW_CLASSIC_MAX_NODES) && (node < ZW_LR_MIN_NODE_ID))) {
    return 0;
  }

  if (node == 0) {
    for (i = 0; i < ZW_MAX_NODES; i++) {
      if (ndb[i]) {
        return list_head(ndb[i]->endpoints);
      }
    }
  } else if (ndb[node - 1]) {
    return list_head(ndb[node - 1]->endpoints);
  }
  return 0;
}

rd_ep_database_entry_t* rd_ep_next(nodeid_t node, rd_ep_database_entry_t* ep)
{
  nodeid_t i;
  rd_ep_database_entry_t* next = list_item_next(ep);

  if (next == 0 && node == 0) {
    for (i = ep->node->nodeid; i < ZW_MAX_NODES; i++) {
      if (ndb[i]) {
        return list_head(ndb[i]->endpoints);
      }
    }
  }
  return next;
}

rd_node_mode_t rd_node_mode_value_get(nodeid_t n)
{
  rd_node_database_entry_t *node = rd_node_get_raw(n);
  if (node) {
    return RD_NODE_MODE_VALUE_GET(node);
  } else {
    return MODE_NODE_UNDEF;
  }
}

u8_t
rd_get_node_name(rd_node_database_entry_t* n, char* buf, u8_t size)
{
  if (n->nodeNameLen) {
    if (size > n->nodeNameLen) {
      size = n->nodeNameLen;
    }
    memcpy(buf, n->nodename, size);
    return size;
  } else {
    return snprintf(buf, size, "zw%08X%04X", (unsigned int)UIP_HTONL(homeID), n->nodeid);
  }
}

rd_node_database_entry_t* rd_lookup_by_node_name(const char* name)
{
  nodeid_t i;
  uint8_t j;
  char buf[64];
  for (i = 0; i < ZW_MAX_NODES; i++) {
    if (ndb[i]) {
      j = rd_get_node_name(ndb[i], buf, sizeof(buf));
      if (strncasecmp(buf, name, j) == 0) {
        return ndb[i];
      }
    }
  }
  return 0;
}

u8_t rd_get_ep_name(rd_ep_database_entry_t* ep, char* buf, u8_t size)
{
  /* If there is a real name, use that. */
  if (ep->endpoint_name && ep->endpoint_name_len) {
    if (size > ep->endpoint_name_len) {
      size = ep->endpoint_name_len;
    }
    memcpy(buf, ep->endpoint_name, size);
    return size;
  } else {
    /* If there is no name, but there is info, generate a name from the info. */
    if (ep->endpoint_info && (ep->endpoint_info_len > 2)) {
      const char* type_str = get_gen_type_string(ep->endpoint_info[0]);
      return snprintf(buf, size, "%s [%04x%04x%02x]",
                      type_str, (unsigned int)UIP_HTONL(homeID), ep->node->nodeid,
                      ep->endpoint_id);
    } else {
      /* If there is no name and no info, generate a generic name from
       * the homeid, node id, and ep id. */
      return snprintf(buf, size, "(unknown) [%04x%04x%02x]", (unsigned int)UIP_HTONL(homeID),
                      ep->node->nodeid, ep->endpoint_id);
    }
  }
}

u8_t rd_get_ep_location(rd_ep_database_entry_t* ep, char* buf, u8_t size)
{
  if (size > ep->endpoint_loc_len) {
    size = ep->endpoint_loc_len;
  }

  memcpy(buf, ep->endpoint_location, size);
  return size;
}

void rd_node_add_dsk(nodeid_t node, uint8_t dsklen, const uint8_t *dsk)
{
  rd_node_database_entry_t *nd;

  if ((dsklen == 0) || (dsk == NULL)) {
    return;
  }

  nd = rd_node_get_raw(node);
  if (nd) {
    rd_node_database_entry_t* old_nd = rd_lookup_by_dsk(dsklen, dsk);
    if (old_nd) {
      /* Unlikely, but possible: the same device gets added again. */
      LOG_PRINTF("New node id %d replaces existing node id %d for device with this dsk.\n",
                 node, old_nd->nodeid);
      rd_data_mem_free(old_nd->dsk);
      old_nd->dsk = NULL;
      old_nd->dskLen = 0;
      /* TODO: Should the node id also be set failing here? */
    }
    if (nd->dskLen != 0) {
      /* TODO: this is not supposed to happen - replace DSK is not supported */
      assert(nd->dskLen == 0);
      if (nd->dsk != NULL) {
        LOG_PRINTF("Replacing old dsk\n");
        /* Silently replace, for now */
        rd_data_mem_free(nd->dsk);
        nd->dskLen = 0;
      }
    }

    nd->dsk = rd_data_mem_alloc(dsklen);
    if (nd->dsk) {
      memcpy(nd->dsk, dsk, dsklen);
      nd->dskLen = dsklen;
      LOG_PRINTF("Setting dsk 0x%02x%02x%02x%02x... on node %u.\n",
                 dsk[0], dsk[1], dsk[2], dsk[3], node);

      /* Insert other fields from pvl. */
    } else {
      /* TODO: should we return an error here. */
      nd->dskLen = 0;
    }
  }
  /* TODO: should we return an error if no nd?. */
}

rd_node_database_entry_t* rd_lookup_by_dsk(uint8_t dsklen, const uint8_t* dsk)
{
  nodeid_t ii;

  if (dsklen == 0) {
    return NULL;
  }

  for (ii = 0; ii < ZW_MAX_NODES; ii++) {
    if (ndb[ii] && (ndb[ii]->dskLen >= dsklen)) {
      if (memcmp(ndb[ii]->dsk, dsk, dsklen) == 0) {
        return ndb[ii];
      }
    }
  }
  return NULL;
}

rd_node_database_entry_t* rd_get_node_dbe(nodeid_t nodeid)
{
  //ASSERT(nodeid>0);
  if (nodemask_nodeid_is_invalid(nodeid)) {
    ERR_PRINTF("Invalid node id\n");
    return 0;
  }

  if (ndb[nodeid - 1]) {
    ndb[nodeid - 1]->refCnt++;
  }
  return ndb[nodeid - 1];
}
