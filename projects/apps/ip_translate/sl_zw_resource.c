/*******************************************************************************
 * @file  sl_zw_resource.c
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
#include <stdlib.h>
#include <strings.h>
#include <stdio.h>
#include <stdbool.h>
#include "ctype.h"
#include "sl_common_log.h"
#include "sl_common_config.h"
#include "sl_gw_info.h"
#include "sl_sleeptimer.h"
#include "apps/transport/sl_zw_send_data.h"
#include "Serialapi.h"
#include "utls/sl_node_sec_flags.h"
#include "modules/sl_rd_data_store.h"
#include "sl_zw_resource.h"
#include "ZW_transport_api.h"
#include "ZW_classcmd.h"
#include "sl_uip_def.h"
#include "apps/Z-Wave/CC/CC_NetworkManagement.h"
#include "apps/Z-Wave/CC/RD_internal.h"
#include "apps/transport/sl_zw_send_request.h"
#include "apps/transport/sl_ts_common.h"
#include "sl_rd_types.h"
#include "sl_sleeptimer.h"
#include "ip_bridge/sl_bridge.h"
#include "threads/sl_zw_netif.h"

//This is 2000ms
#define SL_REQUEST_TIMEOUT_MS 2000

// This is define from ZW_transport.h in the protocol.
#define ZWAVE_NODEINFO_CONTROLLER_NODE 0x02

typedef char rd_name_t[64];
int dont_set_cache_flush_bit = 0;
static sl_sleeptimer_timer_handle_t dead_node_timer;
static sl_sleeptimer_timer_handle_t find_report_timer;
static sl_sleeptimer_timer_handle_t nif_request_timer;
sl_sleeptimer_timer_handle_t ep_probe_update_timer;
sl_sleeptimer_timer_handle_t send_suc_id_timer;
/* RD probe lock. If this lock is set then the probe machine is locked. */
static uint8_t probe_lock          = 0;
uint8_t denied_conflict_probes     = 0;
static uint8_t identical_endpoints = 0;
/* This global, when set to 1, indicates that the ZIP GW has entered an existing network
 * and has assigned itself the SUC/SIS role. The ZGW must proceed to inform all existing
 * nodes about the presence of itself as SUC/SIS and will then clear this flag. */
uint8_t suc_changed = 0;

/* forward */
void rd_node_probe_update(rd_node_database_entry_t *data);
void rd_ep_probe_update(rd_ep_database_entry_t *ep);
void send_suc_id(uint8_t);

/** Check if endpoint supports a command class non-securely.
 *
 * \param ep Valid pointer to an endpoint.
 * \param class The command class to check for.
 * \return True is class is supported by ep, otherwise false.
 */
static int rd_ep_supports_cmd_class_nonsec(rd_ep_database_entry_t *ep,
                                           uint16_t class);

static int sli_cmdclass_flags_supported(nodeid_t nodeid, WORD class);

typedef struct node_probe_done_notifier {
  /** Callback used to report interview result to a requester. \see
   * \ref send_ep_name_reply() for \a ZIP_NAMING_NAME_GET and \a
   * ZIP_NAMING_LOCATION_GET and \ref
   * wakeup_node_was_probed_callback() for \a WUN. */
  void (*callback)(rd_ep_database_entry_t *ep, void *user);
  void *user;
  nodeid_t node_id;
} node_probe_done_notifier_t;

#define NUM_PROBES 1

/** Storage for callbacks to clients that want notification when a
 * node probe is completed.
 *
 * Note that the callback will be triggered when the probing state
 * machine reaches a final state, ie, both when the probe completes
 * and when it fails.
 *
 * There is only a limited amount of slots, so the client has to
 * handle failure.
 */
static node_probe_done_notifier_t node_probe_notifier[NUM_PROBES];

/** Initialize/reset the array of notifiers.
 *
 * Clear all entries.
 */
static void rd_reset_probe_completed_notifier(void);

/** Call the notifiers if nodeid matches and there are any.
 * Clear the nodeid.
 */
static void rd_trigger_probe_completed_notifier(rd_node_database_entry_t *node);

/* If node state is STATUS_FAILING or STATUS_DONE, check if \p failing
 * matches state.  If not, change state, set failed flag, update mdns,
 * persist the new state, and send a failed node list to client.
 *
 * Do nothing if node state is something else. */
void rd_set_failing(rd_node_database_entry_t *n, uint8_t failing)
{
  rd_node_state_t s;
  s = failing ? STATUS_FAILING : STATUS_DONE;

  if (n->state != s
      && ((n->state == STATUS_FAILING) || (n->state == STATUS_DONE))) {
    DBG_PRINTF("Node %d is now %s\n", n->nodeid, failing ? "failing" : "ok");

    n->state = s;
    n->mode &= ~MODE_FLAGS_FAILED;
    n->mode |= (n->state == STATUS_FAILING) ? MODE_FLAGS_FAILED : 0;

    /*Persisting the Failing Node state*/
    rd_data_store_update(n);

    sl_nm_node_failed_to_unsolicited();
  }
}

/*
   The timeouts are described in Appl Guide(INS13954):

   4.4.1.3.3 AddNodeTimeout: An application MUST implement a timeout for waiting for the protocol
   library to complete inclusion (add). The timeout MUST be calculated according to the formulas
   presented in sections 4.4.1.3.3.1 and 4.4.1.3.3.2.

   4.4.1.3.3.1 New slave: AddNodeTimeout.NewSlave = 76000ms + LISTENINGNODES*217ms + FLIRSNODES*3517ms
   where LISTENINGNODES is the number of listening nodes in the network, and FLIRSNODES is the number
   of nodes in the network that are reached via beaming.

   4.4.1.3.3.2 New controller: AddNodeTimeout.NewController = 76000ms + LISTENINGNODES*217ms +
   FLIRSNODES*3517ms + NETWORKNODES*732ms, where LISTENINGNODES is the number of listening nodes
   in the network, and FLIRSNODES is the number of nodes in the network that are reached via beaming.
   NETWORKNODES is the total number of nodes in the network, i.e. NONLISTENINGNODES + LISTENINGNODES
 + FLIRSNODES.
 */
clock_time_t rd_calculate_inclusion_timeout(BOOL is_controller)
{
  clock_time_t timeout = 76000;
  rd_node_database_entry_t *n;

  for (nodeid_t i = 1; i <= ZW_MAX_NODES; i++) {
    n = rd_node_get_raw(i);

    if (!n) {
      continue;
    }

    if (is_virtual_node(n->nodeid)) {
      continue;
    }

    if (is_controller) {
      timeout += 732;
    }

    if ((n->mode & 0xff) == MODE_FREQUENTLYLISTENING) {
      timeout += 3517;
    }

    if ((n->mode & 0xff) == MODE_ALWAYSLISTENING) {
      timeout += 217;
    }
  }
  DBG_PRINTF(
    "Inclusion timeout (based on the network nodes) calculated is: %lu\n",
    timeout);
  return timeout;
}

static uint8_t rd_add_endpoint(rd_node_database_entry_t *n, BYTE epid)
{
  rd_ep_database_entry_t *ep;

  ep = (rd_ep_database_entry_t *) rd_data_mem_alloc(
    sizeof(rd_ep_database_entry_t));
  if (!ep) {
    ERR_PRINTF("Out of memory\n");
    return 0;
  }

  memset(ep, 0, sizeof(rd_ep_database_entry_t));

  ep->endpoint_id = epid;
  ep->state       = EP_STATE_PROBE_INFO;
  ep->node        = n;

  /*A name of null means the default name should be used.*/
  ep->endpoint_name_len = 0;
  ep->endpoint_name     = 0;
  list_add(n->endpoints, ep);
  n->nEndpoints++;

  return 1;
}

static int rd_cap_wake_up_callback(BYTE txStatus,
                                   BYTE rxStatus,
                                   ZW_APPLICATION_TX_BUFFER *pCmd,
                                   WORD cmdLength,
                                   void *user)
{
  (void) rxStatus;
  (void) cmdLength;

  uint32_t min_interval = 0;
  uint32_t max_interval = 0;
  uint32_t step         = 0;

  rd_node_database_entry_t *e = (rd_node_database_entry_t *) user;
  if (txStatus == TRANSMIT_COMPLETE_OK) {
    min_interval = pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
                   .minimumWakeUpIntervalSeconds1
                   << 16
                   | pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
                   .minimumWakeUpIntervalSeconds2
                   << 8
                   | pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
                   .minimumWakeUpIntervalSeconds3;

    max_interval = pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
                   .maximumWakeUpIntervalSeconds1
                   << 16
                   | pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
                   .maximumWakeUpIntervalSeconds2
                   << 8
                   | pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
                   .maximumWakeUpIntervalSeconds3;

    step = pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
           .wakeUpIntervalStepSeconds1
           << 16
           | pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
           .wakeUpIntervalStepSeconds2
           << 8
           | pCmd->ZW_WakeUpIntervalCapabilitiesReportV2Frame
           .wakeUpIntervalStepSeconds3;

    DBG_PRINTF("min: %ld max: %ld\n step:%ld",
               min_interval,
               max_interval,
               step);
    if ((min_interval < DEFAULT_WAKE_UP_INTERVAL)
        && (DEFAULT_WAKE_UP_INTERVAL < max_interval)) {
      e->wakeUp_interval =
        (DEFAULT_WAKE_UP_INTERVAL - (DEFAULT_WAKE_UP_INTERVAL % step));
      if (e->wakeUp_interval < min_interval) {
        e->wakeUp_interval =
          (DEFAULT_WAKE_UP_INTERVAL + (DEFAULT_WAKE_UP_INTERVAL % step));
      }
    } else if (min_interval > DEFAULT_WAKE_UP_INTERVAL) {
      e->wakeUp_interval = min_interval;
    } else if (max_interval < DEFAULT_WAKE_UP_INTERVAL) {
      e->wakeUp_interval = max_interval;
    }
    DBG_PRINTF("Wakeup Interval set to %ld\n", e->wakeUp_interval);
    e->state = STATUS_SET_WAKE_UP_INTERVAL;
  } else {
    e->state = STATUS_ASSIGN_RETURN_ROUTE;
  }
  rd_node_probe_update(e);
  return 0;
}

static int rd_probe_vendor_callback(BYTE txStatus,
                                    BYTE rxStatus,
                                    ZW_APPLICATION_TX_BUFFER *pCmd,
                                    WORD cmdLength,
                                    void *user)
{
  (void) rxStatus;
  (void) cmdLength;

  rd_node_database_entry_t *e = (rd_node_database_entry_t *) user;

  if (txStatus == TRANSMIT_COMPLETE_OK) {
    e->manufacturerID =
      pCmd->ZW_ManufacturerSpecificReportFrame.manufacturerId1 << 8
        | pCmd->ZW_ManufacturerSpecificReportFrame.manufacturerId2;
    e->productID = pCmd->ZW_ManufacturerSpecificReportFrame.productId1 << 8
                   | pCmd->ZW_ManufacturerSpecificReportFrame.productId2;
    e->productType = pCmd->ZW_ManufacturerSpecificReportFrame.productTypeId1
                     << 8
                     | pCmd->ZW_ManufacturerSpecificReportFrame.productTypeId2;
  } else {
    ERR_PRINTF("rd_probe_vendor_callback: manufacturer report received fail\n");
  }
  /* Move to the next state in any case since the failure of manufacturer report
   * is independent of multi channel probing */
  e->state = STATUS_ENUMERATE_ENDPOINTS;
  rd_node_probe_update(e);
  return 0;
}

void find_report_timed_out(sl_sleeptimer_timer_handle_t *handle, void *user)
{
  (void) handle;
  (void) user;
  printf("find report timed out\n");
}

static int rd_ep_find_callback(BYTE txStatus,
                               BYTE rxStatus,
                               ZW_APPLICATION_TX_BUFFER *pCmd,
                               WORD cmdLength,
                               void *user)
{
  (void) rxStatus;

  rd_node_database_entry_t *n = (rd_node_database_entry_t *) user;
  uint8_t *cmd                = (uint8_t *) pCmd;
  int endpoint_offset =
    offsetof(ZW_MULTI_CHANNEL_END_POINT_FIND_REPORT_1BYTE_V4_FRAME,
             variantgroup1);
  int n_end_points = (cmdLength - endpoint_offset);
  int epid         = 0;

  sl_sleeptimer_stop_timer(&find_report_timer);
  if (txStatus == TRANSMIT_COMPLETE_OK && n->state == STATUS_FIND_ENDPOINTS) {
    if ((uint8_t) cmd[endpoint_offset] == 0) {
      n_end_points = 0;
    }

    DBG_PRINTF("Node id: %d has: %d endpoints in this find report\n",
               n->nodeid,
               n_end_points);

    for (nodeid_t i = 0; i < n_end_points; i++) {
      epid = ((uint8_t) cmd[i + endpoint_offset]) & 0x7f;
      if (!rd_add_endpoint(n, epid)) {
        n->state = STATUS_PROBE_FAIL;
        rd_node_probe_update(n);
        return 0;
      }
    }
    for (nodeid_t i = 0; i < (n->nAggEndpoints); i++) {
      if (!rd_add_endpoint(n, epid + i + 1)) {
        n->state = STATUS_PROBE_FAIL;
        rd_node_probe_update(n);
        return 0;
      }
    }

    if (pCmd->ZW_MultiChannelEndPointFindReport1byteV4Frame.reportsToFollow
        != 0) {
      sl_sleeptimer_start_timer_ms(&find_report_timer,
                                   100,
                                   find_report_timed_out,
                                   n,
                                   1,
                                   0);
      return 1; // Tell sl_zw_send_request to wait for more reports
    }
  } else {
    ERR_PRINTF("rd_ep_find_callback: multichannel endpoint find report"
               "receive failed or timedout\n");
  }
  n->state = STATUS_CHECK_WU_CC_VERSION;
  rd_node_probe_update(n);
  return 0;
}

static int rd_ep_get_callback(BYTE txStatus,
                              BYTE rxStatus,
                              ZW_APPLICATION_TX_BUFFER *pCmd,
                              WORD cmdLength,
                              void *user)
{
  (void) rxStatus;

  int n_end_points;
  int n_aggregated_endpoints;

  rd_node_database_entry_t *n = (rd_node_database_entry_t *) user;
  rd_ep_database_entry_t *ep;

  if (txStatus == TRANSMIT_COMPLETE_OK
      && n->state == STATUS_ENUMERATE_ENDPOINTS) {
    n_end_points =
      pCmd->ZW_MultiChannelEndPointReportV4Frame.properties2 & 0x7f;

    identical_endpoints =
      pCmd->ZW_MultiChannelEndPointReportV4Frame.properties1 & 0x40;
    if (cmdLength >= 5) {
      n_aggregated_endpoints =
        pCmd->ZW_MultiChannelEndPointReportV4Frame.properties3 & 0x7f;
      n_end_points += n_aggregated_endpoints;
    } else {
      n_aggregated_endpoints = 0;
    }

    /*If the old endpoint count does not match free up all endpoints old endpoints except ep0 and create some new ones */
    if (n->nEndpoints != (n_end_points + 1)) {
      rd_ep_database_entry_t *ep0 = list_head(n->endpoints);

      while (ep0->list) {
        ep        = ep0->list;
        ep0->list = ep->list;
        rd_store_mem_free_ep(ep);
      }

      n->nAggEndpoints = n_aggregated_endpoints;
      n->nEndpoints    = 1; //Endpoint 0 is still there
    }

    DBG_PRINTF("Node id: %d has: %d regular endpoints and :%d aggregated "
               "endpoints in endpoint report frame \n",
               n->nodeid,
               n_end_points,
               n->nAggEndpoints);
  } else {
    ERR_PRINTF(
      "rd_ep_get_callback: multichannel endpoint report received fail\n");
  }
  n->state = STATUS_FIND_ENDPOINTS;
  rd_node_probe_update(n);
  return 0;
}

static int rd_probe_wakeup_callback(BYTE txStatus,
                                    BYTE rxStatus,
                                    ZW_APPLICATION_TX_BUFFER *pCmd,
                                    WORD cmdLength,
                                    void *user)
{
  (void) rxStatus;
  (void) cmdLength;

  rd_node_database_entry_t *e = (rd_node_database_entry_t *) user;

  if (txStatus == TRANSMIT_COMPLETE_OK
      && e->state == STATUS_PROBE_WAKE_UP_INTERVAL) {
    e->wakeUp_interval = pCmd->ZW_WakeUpIntervalReportFrame.seconds1 << 16
                         | pCmd->ZW_WakeUpIntervalReportFrame.seconds2 << 8
                         | pCmd->ZW_WakeUpIntervalReportFrame.seconds3 << 0;

    if (pCmd->ZW_WakeUpIntervalReportFrame.nodeid != MyNodeID) {
      WRN_PRINTF("WakeUP notifier NOT set to me!\n");
    }
  } else {
    ERR_PRINTF(
      "rd_probe_wakeup_callback: wake up interval report received fail\n");
  }
  e->state = STATUS_PROBE_ENDPOINTS;
  rd_node_probe_update(e);
  return 0;
}

/*
 * https://graphics.stanford.edu/~seander/bithacks.html#CountBitsSetNaive
 * Counting bits set, Brian Kernighan's way
 */
static inline BYTE bit8_count(BYTE n)
{
  unsigned int c; // c accumulates the total bits set in v
  for (c = 0; n; c++) {
    n &= n - 1; // clear the least sigficant bit set
  }
  return c;
}

static BYTE count_array_bits(BYTE *a, BYTE length)
{
  BYTE c = 0;
  for (BYTE i = 0; i < length; i++) {
    c += bit8_count(a[i]);
  }
  return c;
}

static void
sli_rd_ep_aggregated_members_get_calc_index(rd_ep_database_entry_t *ep,
                                            ZW_APPLICATION_TX_BUFFER *pCmd)
{
  unsigned int j;
  unsigned int n;
  n = 0;
  // Accessing flexible array of bitmasks after struct as per Z-Wave spec
  // The first 4 bytes are the header, so we start from index 4
  for (nodeid_t i = 4;
       i < sizeof(ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_4BYTE_V4_FRAME);
       i++) {
    uint8_t c = ((uint8_t *) pCmd)[i];

    j = 0;
    while (c) {
      if (c & 1) {
        ep->endpoint_agg[ep->endpoint_aggr_len] = (uint8_t) (n + j + 1);
        ep->endpoint_aggr_len++;
      }
      c = c >> 1;
      j++;
    }
    n = n + 8;
  }
}

static int rd_ep_aggregated_members_get_callback(BYTE txStatus,
                                                 BYTE rxStatus,
                                                 ZW_APPLICATION_TX_BUFFER *pCmd,
                                                 WORD cmdLength,
                                                 void *user)
{
  (void) rxStatus;
  (void) cmdLength;

  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t *) user;
  ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_4BYTE_V4_FRAME *f =
    (ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_4BYTE_V4_FRAME *) pCmd;
  int n;

  if (ep->state != EP_STATE_PROBE_AGGREGATED_ENDPOINTS) {
    return 0;
  }

  if (txStatus == TRANSMIT_COMPLETE_OK) {
    n = count_array_bits(&f->aggregatedMembersBitMask1, f->numberOfBitMasks);
    if (ep->endpoint_agg) {
      rd_data_mem_free(ep->endpoint_agg);
    }
    ep->endpoint_agg = rd_data_mem_alloc(n);
    if (!ep->endpoint_agg) {
      ERR_PRINTF("Out of memory\n");
      ep->state = EP_STATE_PROBE_FAIL;
      rd_ep_probe_update(ep);
      return 0;
    }
    ep->endpoint_aggr_len = 0;

    /*Calculate the index of the bits set in the mask*/
    sli_rd_ep_aggregated_members_get_calc_index(ep, pCmd);
  }

  DBG_PRINTF("This is aggregated enpdoint. Not doing any more probing on it\n");
  ep->state = EP_STATE_PROBE_DONE;
  rd_ep_probe_update(ep);
  return 0;
}

static int rd_ep_capability_get_callback(BYTE txStatus,
                                         BYTE rxStatus,
                                         ZW_APPLICATION_TX_BUFFER *pCmd,
                                         WORD cmdLength,
                                         void *user)
{
  (void) rxStatus;
  (void) cmdLength;

  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t *) user;

  if (txStatus == TRANSMIT_COMPLETE_OK
      && ep->endpoint_id
      == (pCmd->ZW_MultiChannelCapabilityReport1byteV4Frame.properties1
          & 0x7F)) {
    if (ep->endpoint_info) {
      rd_data_mem_free(ep->endpoint_info);
    }
    ep->endpoint_info_len = 0;

    ep->endpoint_info = rd_data_mem_alloc(cmdLength - 3);
    WRN_PRINTF("Storing %i bytes epid = %i\n", cmdLength - 3, ep->endpoint_id);
    if (ep->endpoint_info) {
      memcpy(ep->endpoint_info,
             &(pCmd->ZW_MultiChannelCapabilityReport1byteV4Frame
               .genericDeviceClass),
             cmdLength - 3);
      ep->endpoint_info_len = cmdLength - 3;
      ep->state             = EP_STATE_PROBE_SEC2_C2_INFO;
    } else {
      ep->state = EP_STATE_PROBE_FAIL;
    }
  } else {
    ep->state = EP_STATE_PROBE_FAIL;
  }
  rd_ep_probe_update(ep);
  return 0;
}

static int rd_ep_zwave_plus_info_callback(BYTE txStatus,
                                          BYTE rxStatus,
                                          ZW_APPLICATION_TX_BUFFER *pCmd,
                                          WORD cmdLength,
                                          void *user)
{
  (void) rxStatus;
  (void) cmdLength;

  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t *) user;

  if (txStatus == TRANSMIT_COMPLETE_OK) {
    if (cmdLength >= sizeof(ZW_ZWAVEPLUS_INFO_REPORT_V2_FRAME)) {
      ep->installer_iconID =
        pCmd->ZW_ZwaveplusInfoReportV2Frame.installerIconType1 << 8
          | pCmd->ZW_ZwaveplusInfoReportV2Frame.installerIconType2;

      ep->user_iconID = pCmd->ZW_ZwaveplusInfoReportV2Frame.userIconType1 << 8
                        | pCmd->ZW_ZwaveplusInfoReportV2Frame.userIconType2;

      if (pCmd->ZW_ZwaveplusInfoReportV2Frame.roleType
          == ROLE_TYPE_SLAVE_PORTABLE) {
        ep->node->node_properties_flags |= RD_NODE_FLAG_PORTABLE;
      }
    }
  } else {
    ERR_PRINTF("rd_ep_zwave_plus_info_callback: zwave plus info report "
               "received fail\n");
  }
  ep->state = EP_STATE_MDNS_PROBE;
  rd_ep_probe_update(ep);
  return 0;
}

static void sli_rd_ep_secure_commands_done(uint8_t cmdLength,
                                           uint8_t header_len,
                                           rd_ep_database_entry_t *ep,
                                           ZW_APPLICATION_TX_BUFFER *pCmd,
                                           uint8_t flag)
{
  if (cmdLength > header_len) {
    /*Reallocate the node info to hold the security nif*/
    uint8_t *p =
      rd_data_mem_alloc(ep->endpoint_info_len + 2 + cmdLength - header_len);
    if (!p) {
      return;
    }
    if (ep->endpoint_info) {
      memcpy(p, ep->endpoint_info, ep->endpoint_info_len);
      rd_data_mem_free(ep->endpoint_info);
    }

    ep->endpoint_info = p;
    p += ep->endpoint_info_len;

    /* Insert security command class mark */
    *p++ = (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 8);
    *p++ = (COMMAND_CLASS_SECURITY_SCHEME0_MARK >> 0) & 0xFF;

    memcpy(p, &((BYTE *) pCmd)[header_len], cmdLength - header_len);
    ep->endpoint_info_len += 2 + cmdLength - header_len;
  }

  if (ep->endpoint_id == 0) {
    DBG_PRINTF("Setting flag 0x%02x\n", flag);
    ep->node->security_flags |= flag;
  }
}

static int rd_ep_secure_commands_get_callback(BYTE txStatus,
                                              BYTE rxStatus,
                                              ZW_APPLICATION_TX_BUFFER *pCmd,
                                              WORD cmdLength,
                                              void *user)
{
  (void) rxStatus;

  rd_ep_database_entry_t *ep = (rd_ep_database_entry_t *) user;
  uint8_t header_len;
  uint8_t flag;

  if (ep->state == EP_STATE_PROBE_SEC0_INFO) {
    header_len = 3;
  } else {
    header_len = 2;
  }

  switch (ep->state) {
    case EP_STATE_PROBE_SEC2_C2_INFO:
      flag = NODE_FLAG_SECURITY2_ACCESS;
      break;
    case EP_STATE_PROBE_SEC2_C1_INFO:
      flag = NODE_FLAG_SECURITY2_AUTHENTICATED;
      break;
    case EP_STATE_PROBE_SEC2_C0_INFO:
      flag = NODE_FLAG_SECURITY2_UNAUTHENTICATED;
      break;
    case EP_STATE_PROBE_SEC0_INFO:
      flag = NODE_FLAG_SECURITY0;
      break;
    default:
      flag = 0;
      ASSERT(0);
      break;
  }

  DBG_PRINTF("Security flags 0x%02x\n", ep->node->security_flags);
  /* If ep is the real node and it was added by someone else and it
   * has not been probed before, we allow probe fail to down-grade the
   * security flags. */
  if ((ep->endpoint_id == 0)
      && !(ep->node->node_properties_flags & RD_NODE_FLAG_ADDED_BY_ME)
      && (ep->node->node_properties_flags & RD_NODE_FLAG_JUST_ADDED)) {
    if (ep->node->security_flags & flag) {
      DBG_PRINTF("Clearing flag 0x%02x\n", flag);
    }
    ep->node->security_flags &= ~flag;
  }

  if (txStatus == TRANSMIT_COMPLETE_OK) {
    sli_rd_ep_secure_commands_done(cmdLength, header_len, ep, pCmd, flag);
  }
  if (ep->endpoint_id > 0
      || (ep->endpoint_id == 0 && ep->state == EP_STATE_PROBE_SEC0_INFO)) {
    /* Endpoints are only queried about one security class while root device
     * will run through all security probing.
     *
     * GW should try to probe versions after we done the security probing.
     * EP_STATE_PROBE_SEC0_INFO is used as an ending state to indicate the root device
     * has finished security probing and state should be moved to PROBE_VERSION.
     */
    ep->state = EP_STATE_PROBE_VERSION;
  } else {
    /* The node itself is dragged through all the states. */
    ep->state++;
  }
  rd_ep_probe_update(ep);
  return 0;
}

/*Used when a get node info is pending */
static rd_ep_database_entry_t *nif_request_ep = 0;

/** The node currently being probed (pan side or mdns).
 *
 * Used to determine if the probe machine is busy.  I.e., it should
 * not be cleared before all nodes are in one of the terminal states.
 *
 * Used by assign return route, node_info_request_timeout(),
 * rd_endpoint_name_probe_done() to determine which node the callback
 * concerns.  I.e., a node must not be removed (rd_remove_node())
 * while these operations are in progress.
 *
 */
static rd_node_database_entry_t *current_probe_entry = 0;

static void sli_rd_nif_request_notf_done(rd_ep_database_entry_t *ep,
                                         uint8_t *nif,
                                         uint8_t nif_len)
{
  if (!ep->endpoint_info) {
    ep->state = EP_STATE_PROBE_FAIL;
    return;
  }
  memcpy(ep->endpoint_info, nif + 1, nif_len - 1);

  ep->endpoint_info[nif_len - 1] = COMMAND_CLASS_ZIP_NAMING;
  ep->endpoint_info[nif_len]     = COMMAND_CLASS_ZIP;

  ep->endpoint_info_len = nif_len + 1;

  /* If node is just added and GW is inclusion controller, we
   * are still trying to determine security classes. */
  if ((ep->endpoint_id == 0)
      && !(ep->node->node_properties_flags & RD_NODE_FLAG_ADDED_BY_ME)
      && (ep->node->node_properties_flags & RD_NODE_FLAG_JUST_ADDED)) {
    /* Downgrade S2 flags if node does not support it. */
    if (!rd_ep_supports_cmd_class_nonsec(ep, COMMAND_CLASS_SECURITY_2)) {
      ep->node->security_flags &= ~NODE_FLAGS_SECURITY2;
    }
    /* Downgrade S0 if node does not support it. */
    if (!rd_ep_supports_cmd_class_nonsec(ep, COMMAND_CLASS_SECURITY)) {
      ep->node->security_flags &= ~NODE_FLAG_SECURITY0;
    }
  }
  if (ep->endpoint_id == 0) {
    /* The version knowledge GW has is related to which set of command class
     * the node supports. We need to reset the version here together with
     * endpoint_info.
     */
    rd_node_cc_versions_set_default(ep->node);
    ep->node->node_version_cap_and_zwave_sw = 0;
    ep->node->probe_flags                   = RD_NODE_PROBE_NEVER_STARTED;
    ep->node->node_is_zws_probed            = 0;
  }

  ep->state++;
}

/*IN  Node id of the node that send node info */
/*IN  Pointer to Application Node information */
/*IN  Node info length                        */
void rd_nif_request_notify(uint8_t bStatus,
                           nodeid_t bNodeID,
                           uint8_t *nif,
                           uint8_t nif_len)
{
  rd_ep_database_entry_t *ep = nif_request_ep;
  if (ep && ep->state == EP_STATE_PROBE_INFO) {
    nif_request_ep = 0;
    sl_sleeptimer_stop_timer(&nif_request_timer);

    ASSERT(ep->node);
    if (bStatus && ep->node->nodeid == bNodeID) {
      ep->node->nodeType = nif[0];
      if (ep->endpoint_info) {
        rd_data_mem_free(ep->endpoint_info);
      }
      ep->endpoint_info = rd_data_mem_alloc(nif_len + 1);
      sli_rd_nif_request_notf_done(ep, nif, nif_len);
    } else {
      ep->state = EP_STATE_PROBE_FAIL;
    }
    rd_ep_probe_update(ep);
  }
}

void sl_assign_route_callback(uint8_t status)
{
  if (current_probe_entry) {
    if (status != TRANSMIT_COMPLETE_OK) {
      ERR_PRINTF("sl_assign_route_callback: assign return route fail\n");
    }
    current_probe_entry->state = STATUS_PROBE_WAKE_UP_INTERVAL;
    rd_node_probe_update(current_probe_entry);
  } else {
    ASSERT(0);
  }
}

static int should_probe_level(rd_ep_database_entry_t *ep, uint8_t level)
{
  /* If this gateway added the node and If the root node does not support this security class. Do not probe it. */
  /* Inclusion controllers should still probe this because they do not know which keys are granted */
  if ((ep->endpoint_id == 0)                       //root endpoint
      && (0 == (ep->node->security_flags & level)) //security level granted
      && (ep->node->node_properties_flags
          & RD_NODE_FLAG_ADDED_BY_ME)) { //and node added by me))
    DBG_PRINTF("This gateway (nodeid: %d) added this node (nodeid: %d) with "
               "%02x security keys greanted but is not "
               "granted this level security key (%02x). This gateway won't "
               "probe S2 at this level\n",
               MyNodeID,
               ep->node->nodeid,
               ep->node->security_flags,
               level);
    return FALSE;
  }
  return TRUE;
}

BOOL is_virtual_node(nodeid_t nid);

static int sli_rd_ep_probe_update_aggegate(rd_ep_database_entry_t *ep)
{
  ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME f;
  f.cmdClass    = COMMAND_CLASS_MULTI_CHANNEL_V4;
  f.cmd         = MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4;
  f.properties1 = ep->endpoint_id;
  ts_param_t p;
  ts_set_std(&p, ep->node->nodeid);

  if (!sl_zw_send_request(&p,
                          (BYTE *) &f,
                          sizeof(ZW_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME),
                          MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4,
                          SL_REQUEST_TIMEOUT_MS,
                          ep,
                          rd_ep_aggregated_members_get_callback)) {
    return FALSE;
  }
  return TRUE;
}

static int sli_rd_ep_probe_update_info(rd_ep_database_entry_t *ep)
{
  static ZW_MULTI_CHANNEL_CAPABILITY_GET_V2_FRAME cap_get_frame = {
    COMMAND_CLASS_MULTI_CHANNEL_V2,
    MULTI_CHANNEL_CAPABILITY_GET_V2,
    0
  };
  /*The Device Capabilities of Endpoint 1 must respond the normal NIF*/
  if (ep->endpoint_id > 0) {
    // Check if this endpoint is aggregated, we need to locate its offset in
    // the list. Ie:
    // nEndpoints = 4,  nAggr = 1
    // root idx = 1
    // ep1  idx = 2
    // ep4  idx = 3
    // ep5  idx = 4 (aggr)
    int ep_idx = 0;
    for (rd_ep_database_entry_t *s = list_head(ep->node->endpoints); s;
         s                         = list_item_next(s)) {
      ep_idx++;
      if (s == ep) {
        break;
      }
    }

    ASSERT(ep_idx <= ep->node->nEndpoints);
    // If the endpoint index is in the aggregated range we need to do and
    // AGGREGATED_MEMBERS_GET otherwise we go to security probing.
    int last_regular_ep_idx = ep->node->nEndpoints - ep->node->nAggEndpoints;
    if (ep_idx > last_regular_ep_idx) {
      DBG_PRINTF("Probing aggregated endpoint: %d", ep_idx);
      ep->state = EP_STATE_PROBE_AGGREGATED_ENDPOINTS;
      rd_ep_probe_update(ep);
      return TRUE;
    } else {
      cap_get_frame.properties1 = ep->endpoint_id & 0x7F;

      ts_param_t p;
      ts_set_std(&p, ep->node->nodeid);
      if (!sl_zw_send_request(&p,
                              (BYTE *) &cap_get_frame,
                              sizeof(cap_get_frame),
                              MULTI_CHANNEL_CAPABILITY_REPORT_V2,
                              SL_REQUEST_TIMEOUT_MS,
                              ep,
                              rd_ep_capability_get_callback)) {
        return FALSE;
      }
    }
  }

  ERR_PRINTF("Request node info %d\n", ep->node->nodeid);
  return TRUE;
}

static int sli_rd_ep_probe_update_c2_info(rd_ep_database_entry_t *ep)
{
  uint8_t secure_commands_supported_get2[] = {
    COMMAND_CLASS_SECURITY_2,
    SECURITY_2_COMMANDS_SUPPORTED_GET
  };
  ts_param_t p;
  ts_set_std(&p, ep->node->nodeid);
  /*If this is not the root NIF just use the auto scheme */
  if (ep->endpoint_id > 0) {
    p.scheme = AUTO_SCHEME;
    if (((sl_get_cache_entry_flag(ep->node->nodeid) & NODE_FLAGS_SECURITY2))
        && ((sl_get_cache_entry_flag(MyNodeID) & NODE_FLAGS_SECURITY2))) {
      /* S2 is supported and working on both node and GW.
       * Therefore ep must be S2. */
      if (!sl_zw_send_request(&p,
                              secure_commands_supported_get2,
                              sizeof(secure_commands_supported_get2),
                              SECURITY_2_COMMANDS_SUPPORTED_REPORT,
                              20,
                              ep,
                              rd_ep_secure_commands_get_callback)) {
        return FALSE;
      }
    }
    /* Node is not included with S2 or GW is not running S2. */
    /* If the ep or the GW do not support S0, either, we let the
     * PROBE_SEC0 state sort it out. */
    ep->state = EP_STATE_PROBE_SEC0_INFO - 1;
    return TRUE;
  }
  /* Root device */
  if (sl_get_cache_entry_flag(MyNodeID) & NODE_FLAG_SECURITY2_ACCESS) {
    p.scheme = SECURITY_SCHEME_2_ACCESS;
    if (!sl_zw_send_request(&p,
                            secure_commands_supported_get2,
                            sizeof(secure_commands_supported_get2),
                            SECURITY_2_COMMANDS_SUPPORTED_REPORT,
                            20,
                            ep,
                            rd_ep_secure_commands_get_callback)) {
      return FALSE;
    }
  }
  return TRUE;
}

static int sli_rd_ep_probe_update_info_zwave_plus(rd_ep_database_entry_t *ep)
{
  ep->installer_iconID = 0;
  ep->user_iconID      = 0;
  ts_param_t p;
  ts_set_std(&p, ep->node->nodeid);

  if ((rd_ep_class_support(ep, COMMAND_CLASS_ZWAVEPLUS_INFO) & SUPPORTED)) {
    uint8_t zwave_plus_info_get[] = { COMMAND_CLASS_ZWAVEPLUS_INFO,
                                      ZWAVEPLUS_INFO_GET };
    p.scheme                      = AUTO_SCHEME;
    if (!sl_zw_send_request(&p,
                            zwave_plus_info_get,
                            sizeof(zwave_plus_info_get),
                            ZWAVEPLUS_INFO_REPORT,
                            SL_REQUEST_TIMEOUT_MS,
                            ep,
                            rd_ep_zwave_plus_info_callback)) {
      return FALSE;
    }
  }
  return TRUE;
}

static int sli_rd_ep_probe_update_s0(rd_ep_database_entry_t *ep)
{
  uint8_t secure_commands_supported_get[] = { COMMAND_CLASS_SECURITY,
                                              SECURITY_COMMANDS_SUPPORTED_GET };
  ts_param_t p;
  ts_set_std(&p, ep->node->nodeid);
  /* Do not probe S0 if it is not supported by the node. */
  /* is_lr_node() is workaround for LR nodes incorrectly advertizing S0 in the NIF */
  if (!sl_cmdclass_supported(ep->node->nodeid, COMMAND_CLASS_SECURITY)
      || is_lr_node(ep->node->nodeid)) {
    DBG_PRINTF("Node (%d) does not support COMMAND_CLASS_SECURITY, will "
               "not probe S0 for it",
               ep->node->nodeid);
    ep->state = EP_STATE_PROBE_VERSION;
    rd_ep_probe_update(ep);
    return TRUE;
  }

  /* Do not probe S0 on a real endpoint if the root device is not
   * using it.  */
  if ((ep->endpoint_id > 0)
      && !(sl_get_cache_entry_flag(ep->node->nodeid) & NODE_FLAG_SECURITY0)) {
    ep->state = EP_STATE_PROBE_VERSION;
    rd_ep_probe_update(ep);
    return TRUE;
  }

  /* Only probe S0 if the GW is using it. */
  if ((sl_get_cache_entry_flag(MyNodeID) & NODE_FLAG_SECURITY0)) {
    p.scheme = SECURITY_SCHEME_0;

    if (!sl_zw_send_request(&p,
                            secure_commands_supported_get,
                            sizeof(secure_commands_supported_get),
                            SECURITY_COMMANDS_SUPPORTED_REPORT,
                            3 * 20,
                            ep,
                            rd_ep_secure_commands_get_callback)) {
      return FALSE;
    }
    return TRUE;
  }
  ep->state = EP_STATE_PROBE_VERSION;
  rd_ep_probe_update(ep);
  return TRUE;
}

static int sli_rd_ep_probe_update_s2_info(rd_ep_database_entry_t *ep)
{
  /* Don't probe more S2 */
  if (!sl_cmdclass_supported(ep->node->nodeid, COMMAND_CLASS_SECURITY_2)) {
    DBG_PRINTF("Node (%d) does not support COMMAND_CLASS_SECURITY_2, will "
               "not probe S2 for it",
               ep->node->nodeid);
    ep->state = EP_STATE_PROBE_SEC0_INFO - 1;
    return TRUE;
  }

  if (!should_probe_level(ep, NODE_FLAG_SECURITY2_ACCESS)) {
    return TRUE;
  }

  if (!sli_rd_ep_probe_update_c2_info(ep)) {
    return FALSE;
  }
  return TRUE;
}

static int sli_rd_ep_probe_update_s2_c1_info(rd_ep_database_entry_t *ep)
{
  if (!should_probe_level(ep, NODE_FLAG_SECURITY2_AUTHENTICATED)) {
    return TRUE;
  }

  /*If this is not the root NIF and the root does not support this class, move on*/
  if ((ep->endpoint_id > 0) && (0 == (sl_get_cache_entry_flag(ep->node->nodeid)
                                      & NODE_FLAG_SECURITY2_AUTHENTICATED))) {
    return TRUE;
  }

  if (sl_get_cache_entry_flag(MyNodeID) & NODE_FLAG_SECURITY2_AUTHENTICATED) {
    uint8_t secure_commands_supported_get2[] = {
      COMMAND_CLASS_SECURITY_2,
      SECURITY_2_COMMANDS_SUPPORTED_GET
    };
    ts_param_t p;
    ts_set_std(&p, ep->node->nodeid);
    p.scheme = SECURITY_SCHEME_2_AUTHENTICATED;

    if (!sl_zw_send_request(&p,
                            secure_commands_supported_get2,
                            sizeof(secure_commands_supported_get2),
                            SECURITY_2_COMMANDS_SUPPORTED_REPORT,
                            20,
                            ep,
                            rd_ep_secure_commands_get_callback)) {
      return FALSE;
    }
  }
  return TRUE;
}

static int sli_rd_ep_probe_update_s2_c0_info(rd_ep_database_entry_t *ep)
{
  if (!should_probe_level(ep, NODE_FLAG_SECURITY2_UNAUTHENTICATED)) {
    return TRUE;
  }

  /*If this is not the root NIF and the root does not support this class, move on*/
  if ((ep->endpoint_id > 0) && (0 == (sl_get_cache_entry_flag(ep->node->nodeid)
                                      & NODE_FLAG_SECURITY2_UNAUTHENTICATED))) {
    return TRUE;
  }

  if (sl_get_cache_entry_flag(MyNodeID) & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
    uint8_t secure_commands_supported_get2[] = {
      COMMAND_CLASS_SECURITY_2,
      SECURITY_2_COMMANDS_SUPPORTED_GET
    };
    ts_param_t p;
    ts_set_std(&p, ep->node->nodeid);
    p.scheme = SECURITY_SCHEME_2_UNAUTHENTICATED;

    if (!sl_zw_send_request(&p,
                            secure_commands_supported_get2,
                            sizeof(secure_commands_supported_get2),
                            SECURITY_2_COMMANDS_SUPPORTED_REPORT,
                            20,
                            ep,
                            rd_ep_secure_commands_get_callback)) {
      return FALSE;
    }
  }
  return TRUE;
}

static int sli_rd_ep_probe_update_check(rd_ep_database_entry_t *ep)
{
  ASSERT(current_probe_entry);
  if (!current_probe_entry) {
    return 0;
  }

  if (ep->node == 0) {
    return 0;
  }
  DBG_PRINTF("EP probe rd_node=%i (flags 0x%02x) ep =%d state=%s\n",
             ep->node->nodeid,
             ep->node->security_flags,
             ep->endpoint_id,
             ep_state_name(ep->state));

  if (is_virtual_node(ep->node->nodeid)) {
    rd_remove_node(ep->node->nodeid);
    ASSERT(0);
    return 0;
  }
  return 1;
}

static void rd_ep_probe_handle_aggregated_endpoints(rd_ep_database_entry_t *ep)
{
  if (!sli_rd_ep_probe_update_aggegate(ep)) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
}

static void rd_ep_probe_handle_info(rd_ep_database_entry_t *ep)
{
  if (!sli_rd_ep_probe_update_info(ep)) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
}

static void rd_ep_probe_handle_sec2_c2_info(rd_ep_database_entry_t *ep)
{
  if (ep->node->security_flags & NODE_FLAG_KNOWN_BAD) {
    ep->state = EP_STATE_PROBE_VERSION;
    rd_ep_probe_update(ep);
    return;
  }
  if (sli_rd_ep_probe_update_s2_info(ep) == FALSE) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
  ep->state++;
  rd_ep_probe_update(ep);
}

static void rd_ep_probe_handle_sec2_c1_info(rd_ep_database_entry_t *ep)
{
  if (sli_rd_ep_probe_update_s2_c1_info(ep) == FALSE) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
  ep->state++;
  rd_ep_probe_update(ep);
}

static void rd_ep_probe_handle_sec2_c0_info(rd_ep_database_entry_t *ep)
{
  if (sli_rd_ep_probe_update_s2_c0_info(ep) == FALSE) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
  ep->state++;
  rd_ep_probe_update(ep);
}

static void rd_ep_probe_handle_sec0_info(rd_ep_database_entry_t *ep)
{
  if (!sli_rd_ep_probe_update_s0(ep)) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
}

static void rd_ep_probe_handle_zwave_plus(rd_ep_database_entry_t *ep)
{
  if (!sli_rd_ep_probe_update_info_zwave_plus(ep)) {
    ep->state = EP_STATE_PROBE_FAIL;
    rd_ep_probe_update(ep);
    return;
  }
  ep->state++;
  rd_ep_probe_update(ep);
}

void rd_ep_probe_update(rd_ep_database_entry_t *ep)
{
  if (sli_rd_ep_probe_update_check(ep) == 0) {
    return;
  }

  switch ((int) ep->state) {
    case EP_STATE_PROBE_AGGREGATED_ENDPOINTS:
      rd_ep_probe_handle_aggregated_endpoints(ep);
      break;
    case EP_STATE_PROBE_INFO:
      rd_ep_probe_handle_info(ep);
      break;
    case EP_STATE_PROBE_SEC2_C2_INFO:
      rd_ep_probe_handle_sec2_c2_info(ep);
      break;
    case EP_STATE_PROBE_SEC2_C1_INFO:
      rd_ep_probe_handle_sec2_c1_info(ep);
      break;
    case EP_STATE_PROBE_SEC2_C0_INFO:
      rd_ep_probe_handle_sec2_c0_info(ep);
      break;
    case EP_STATE_PROBE_SEC0_INFO:
      rd_ep_probe_handle_sec0_info(ep);
      break;
    case EP_STATE_PROBE_ZWAVE_PLUS:
      rd_ep_probe_handle_zwave_plus(ep);
      break;
    case EP_STATE_MDNS_PROBE_IN_PROGRESS:
      break;
    case EP_STATE_PROBE_FAIL:
    case EP_STATE_PROBE_DONE:
      rd_node_probe_update(ep->node);
      break;
    default:
      break;
  }
}

void rd_set_wu_interval_callback(BYTE txStatus, void *user, TX_STATUS_TYPE *t)
{
  (void) t;

  rd_node_database_entry_t *n = (rd_node_database_entry_t *) user;

  if (txStatus != TRANSMIT_COMPLETE_OK) {
    WRN_PRINTF("rd_set_wu_interval_callback: set wake up interval fail\n");
  }
  n->state = STATUS_ASSIGN_RETURN_ROUTE;
  rd_node_probe_update(n);
}

/**
 * Read the protocol info from NVM
 */
static void update_protocol_info(rd_node_database_entry_t *n)
{
  NODEINFO ni;

  sl_zw_get_node_proto_info(n->nodeid, &ni);
  n->nodeType = ni.nodeType.basic;

  if (ni.capability & NODEINFO_LISTENING_SUPPORT) {
    n->mode = MODE_ALWAYSLISTENING;
    //n->wakeUp_interval = 70*60; //70 Minutes, the node will be probed once each 70 minutes.
  } else if (ni.security & (NODEINFO_ZWAVE_SENSOR_MODE_WAKEUP_1000
                            | NODEINFO_ZWAVE_SENSOR_MODE_WAKEUP_250)) {
    n->mode = MODE_FREQUENTLYLISTENING;
  } else {
    n->mode = MODE_NONLISTENING;
  }
}

void send_suc_id_cb(BYTE txStatus, TX_STATUS_TYPE *t)
{
  (void) t;
  DBG_PRINTF("send_suc_id_cb\n");
  sl_sleeptimer_stop_timer(&send_suc_id_timer);
  send_suc_id(txStatus);
}

void send_suc_id_slave_cb(BYTE STATUS)
{
  DBG_PRINTF("send_suc_id_slave_cb\n");
  /* Stop the timer or GW ends up false calling send_suc_id() in timeout */
  sl_sleeptimer_stop_timer(&send_suc_id_timer);
  send_suc_id(STATUS);
}

void send_suc_id_timeout(sl_sleeptimer_timer_handle_t *handle, void *use)
{
  (void) handle;
  (void) use;

  send_suc_id(TRANSMIT_COMPLETE_FAIL);
}

void send_suc_id(uint8_t status)
{
  rd_node_database_entry_t *rd_node;
  static nodeid_t current_send_suc_id_dest = 0;

  if (status != TRANSMIT_COMPLETE_OK) {
    ERR_PRINTF("ERROR: sl_zw_send_SUCID or sl_zw_assign_SUC_route to node id: %d "
               "failed\n",
               current_send_suc_id_dest);
  }
  while (current_send_suc_id_dest <= ZW_MAX_NODES) {
    current_send_suc_id_dest++;
    if (current_send_suc_id_dest == MyNodeID) {
      continue;
    }

    rd_node = rd_node_get_raw(current_send_suc_id_dest);
    if (!rd_node) {
      continue;
    }

    if (is_virtual_node(rd_node->nodeid)) {
      continue;
    }

    DBG_PRINTF("Sending SUI ID to node id: %d\n", rd_node->nodeid);
    /* To cover the case of missing callback from either of following calls
     * sl_zw_send_SUCID() or sl_zw_assign_SUC_route()
     */
    sl_sleeptimer_start_timer_ms(&send_suc_id_timer,
                                 1 * CLOCK_SECOND,
                                 send_suc_id_timeout,
                                 0,
                                 1,
                                 0);
    if (isNodeController(rd_node->nodeid)) {
      uint8_t txOption = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE;
      if (!sl_zw_send_SUCID(rd_node->nodeid, txOption, send_suc_id_cb)) {
        ERR_PRINTF("Call to sl_zw_send_SUCID failed\n");
        send_suc_id_cb(TRANSMIT_COMPLETE_FAIL, 0);
      }
      goto exit;
    } else {
      sl_zw_assign_SUC_route(rd_node->nodeid, send_suc_id_slave_cb);
      goto exit;
    }
  }
  exit:
  return;
}

void rd_probe_resume()
{
  nodeid_t i;
  rd_node_database_entry_t *rd_node;

  if (current_probe_entry) {
    DBG_PRINTF("Resume probe of %u\n", current_probe_entry->nodeid);
    rd_node_probe_update(current_probe_entry);
    return;
  }

  for (i = 1; i <= ZW_MAX_NODES; i++) {
    rd_node = rd_node_get_raw(i);
    if (rd_node && (rd_node->state != STATUS_DONE && rd_node->state != STATUS_PROBE_FAIL
                    && rd_node->state != STATUS_FAILING)) {
      current_probe_entry = rd_node;
      rd_node_probe_update(rd_node);
      break;
    }
  }

  if ((i == (ZW_MAX_NODES + 1)) && (probe_lock == 0)) {
    if (suc_changed) {
      suc_changed = 0;
      DBG_PRINTF("Suc changed, Sending new SUC Id to network \n");
      send_suc_id(TRANSMIT_COMPLETE_OK);
    }
  }
}

/**
 *  Lock/Unlock the node probe machine. When the node probe lock is enabled, all probing will stop.
 *  Probing is resumed when the lock is disabled. The probe lock is used during a add node process or during learn mode.
 */
void rd_probe_lock(uint8_t enable)
{
  probe_lock = enable;
  DBG_PRINTF("Probe machine is %slocked\n", (enable) ? "" : "un");

  if (!probe_lock) {
    rd_probe_resume();
  }
}

/**
 * Unlock the probe machine but do not resume probe engine.
 *
 * If probe was locked during NM add node, but the node should not be
 * probed because it is a self-destructing smart start node, this
 * function resets the probe lock.
 *
 * When removal of the node succeeds, \ref current_probe_entry will be
 * reset when the node is deleted.  We also clear \ref
 * current_probe_entry here so that this function can be used in the
 * "removal failed" scenarios.
 */
void rd_probe_cancel(void)
{
  probe_lock          = FALSE;
  current_probe_entry = NULL;
}

u8_t rd_probe_in_progress()
{
  return (current_probe_entry != 0);
}

u8_t rd_node_in_probe(nodeid_t node)
{
  const rd_node_database_entry_t *rd_node;
  rd_node = rd_node_get_raw(node);
  if (rd_node && (rd_node->state != STATUS_DONE && rd_node->state != STATUS_PROBE_FAIL
                  && rd_node->state != STATUS_FAILING)) {
    return 1;
  }
  return 0;
}

/* identical bit guarantees that ALL endpoints are identical. So it is safe to
 * copy endpoint 1 to all other endpoints
 */
void copy_endpoints(rd_ep_database_entry_t *src_ep, rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *dest_ep;
  for (dest_ep = list_head(n->endpoints); dest_ep != NULL;
       dest_ep = list_item_next(dest_ep)) {
    if (dest_ep->endpoint_id > 1) { //only copy to endpoint id 2 and up
      dest_ep->endpoint_location = NULL;

      dest_ep->endpoint_name = NULL;

      dest_ep->endpoint_info = rd_data_mem_alloc(src_ep->endpoint_info_len);
      if (!dest_ep->endpoint_info) {
        ERR_PRINTF("copy_endpoints: no memory for endpoint_info\n");
        return;
      }
      memcpy(dest_ep->endpoint_info,
             src_ep->endpoint_info,
             src_ep->endpoint_info_len);

      dest_ep->endpoint_agg = rd_data_mem_alloc(src_ep->endpoint_aggr_len);
      if (!dest_ep->endpoint_agg) {
        ERR_PRINTF("copy_endpoints: no memory for endpoint_agg\n");
        rd_data_mem_free(dest_ep->endpoint_info);
        return;
      }
      memcpy(dest_ep->endpoint_agg,
             src_ep->endpoint_agg,
             src_ep->endpoint_aggr_len);

      /** Length of #endpoint_info. */
      dest_ep->endpoint_info_len = src_ep->endpoint_info_len;
      /** Length of #endpoint_name. */
      dest_ep->endpoint_name_len = 0;
      /** Length of #endpoint_location. */
      dest_ep->endpoint_loc_len = 0;
      /** Length of aggregations */
      dest_ep->endpoint_aggr_len = src_ep->endpoint_aggr_len;
      dest_ep->state             = src_ep->state;
      /** Z-Wave plus icon ID. */
      dest_ep->installer_iconID = src_ep->installer_iconID;
      /** Z-Wave plus icon ID. */
      dest_ep->user_iconID = src_ep->user_iconID;
    }
  }
}

static void sli_rd_node_probe_update_next(rd_node_database_entry_t *n)
{
  n->state++;
  rd_node_probe_update(n);
}

static void sli_rd_node_probe_update_fail(rd_node_database_entry_t *n)
{
  n->state = STATUS_PROBE_FAIL;
  rd_node_probe_update(n);
}

static void sli_rd_node_probe_update_complete(rd_node_database_entry_t *n)
{
  /* Store all node data in persistent memory */
  rd_data_store_nvm_write(n);
  current_probe_entry = NULL;

  /* If a callback is registered for this node, trigger the callback
   * when the node has reached a final state. */
  rd_trigger_probe_completed_notifier(n);
  rd_probe_resume();
}

static int sli_rd_node_probe_update_node_info(rd_ep_database_entry_t *ep,
                                              rd_node_database_entry_t *n)
{
  if (!ep) { // Abort the probe the node might have been removed
    current_probe_entry = NULL;
    rd_probe_resume();
    return FALSE;
  }

  ASSERT(ep->node == n);

  if (ep->state == EP_STATE_PROBE_FAIL) {
    // DBG_PRINTF("Endpoint probe fail\n");
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
    return FALSE;
  } else if (ep->state != EP_STATE_PROBE_DONE) {
    rd_ep_probe_update(ep);
  } else {
    if (n->security_flags & NODE_FLAG_INFO_ONLY) {
      n->state = STATUS_DONE;
      rd_node_probe_update(n);
    } else {
      sli_rd_node_probe_update_next(n);
    }
  }
  return FALSE;
}

static int sli_rd_node_probe_update_product_id(rd_node_database_entry_t *n)
{
  static ZW_MANUFACTURER_SPECIFIC_GET_FRAME man_spec_get = {
    COMMAND_CLASS_MANUFACTURER_SPECIFIC,
    MANUFACTURER_SPECIFIC_GET
  };
  ts_param_t p;
  ts_set_std(&p, n->nodeid);
  /**
   * Workaround for Schlage door locks with faulty security implementation.
   */
  p.scheme =
    sl_cmdclass_secure_supported(n->nodeid, COMMAND_CLASS_MANUFACTURER_SPECIFIC)
    ? AUTO_SCHEME
    : NO_SCHEME;
  /* Find manufacturer info */
  if (!sl_zw_send_request(&p,
                          (BYTE *) &man_spec_get,
                          sizeof(man_spec_get),
                          MANUFACTURER_SPECIFIC_REPORT,
                          SL_REQUEST_TIMEOUT_MS,
                          n,
                          rd_probe_vendor_callback)) {
    return FALSE;
  }
  return TRUE;
}

static int sli_rd_node_probe_update_check(rd_node_database_entry_t *n)
{
  if (probe_lock) {
    return FALSE;
  }

  if (bridge_state == booting) {
    return FALSE;
  }

  if (n->nodeid == 0) {
    return FALSE;
  }

  if (is_virtual_node(n->nodeid)) {
    rd_remove_node(n->nodeid);
    return FALSE;
  }
  return TRUE;
}

static void sli_rd_node_probe_update_product_id_ex(rd_node_database_entry_t *n)
{
  if (n->nodeid == MyNodeID) {
    n->productID      = router_cfg.product_id;
    n->manufacturerID = router_cfg.manufacturer_id;
    n->productType    = router_cfg.product_type;

    sli_rd_node_probe_update_next(n);
    return;
  }

  if ((sli_cmdclass_flags_supported(n->nodeid, COMMAND_CLASS_MANUFACTURER_SPECIFIC)
       & SUPPORTED) == 0) {
    sli_rd_node_probe_update_next(n);
    return;
  }
  if (sli_rd_node_probe_update_product_id(n) == FALSE) {
    sli_rd_node_probe_update_fail(n);
    return;
  }
}

static void rd_node_probe_handle_created(rd_node_database_entry_t *n)
{
  if (current_probe_entry && current_probe_entry != n) {
    return;
  }
  current_probe_entry = n;
  n->probe_flags      = RD_NODE_FLAG_PROBE_STARTED;
  sli_rd_node_probe_update_next(n);
}

static void rd_node_probe_handle_probe_node_info(rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *ep = list_head(n->endpoints);
  sli_rd_node_probe_update_node_info(ep, n);
}

static void rd_node_probe_handle_enumerate_endpoints(
  rd_node_database_entry_t *n,
  ts_param_t *p,
  ZW_MULTI_CHANNEL_END_POINT_GET_V4_FRAME *multi_ep_get)
{
  if (n->nodeid == MyNodeID
      || ((sli_cmdclass_flags_supported(n->nodeid, COMMAND_CLASS_MULTI_CHANNEL_V4)
           & SUPPORTED) == 0)) {
    n->state++;
    rd_node_probe_update(n);
    return;
  }

  ts_set_std(p, n->nodeid);
  if (!sl_zw_send_request(p,
                          (BYTE *) multi_ep_get,
                          sizeof(*multi_ep_get),
                          MULTI_CHANNEL_END_POINT_REPORT_V4,
                          3 * 20,
                          n,
                          rd_ep_get_callback)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  }
}

static void rd_node_probe_handle_find_endpoints(
  rd_node_database_entry_t *n,
  ts_param_t *p,
  ZW_MULTI_CHANNEL_END_POINT_FIND_V4_FRAME *multi_ep_find)
{
  if (n->nodeid == MyNodeID
      || ((sli_cmdclass_flags_supported(n->nodeid, COMMAND_CLASS_MULTI_CHANNEL_V4)
           & SUPPORTED) == 0)) {
    n->state++;
    rd_node_probe_update(n);
    return;
  }

  ts_set_std(p, n->nodeid);
  if (!sl_zw_send_request(p,
                          (BYTE *) multi_ep_find,
                          sizeof(*multi_ep_find),
                          MULTI_CHANNEL_END_POINT_FIND_REPORT_V4,
                          3 * 20,
                          n,
                          rd_ep_find_callback)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  }
}

static void rd_node_probe_handle_get_wu_cap(rd_node_database_entry_t *n,
                                            ts_param_t *p)
{
  DBG_PRINTF("/n");
  static ZW_WAKE_UP_INTERVAL_CAPABILITIES_GET_V2_FRAME cf;
  cf.cmdClass = COMMAND_CLASS_WAKE_UP_V2;
  cf.cmd      = WAKE_UP_INTERVAL_CAPABILITIES_GET_V2;

  ts_set_std(p, n->nodeid);

  if (!sl_zw_send_request(p,
                          (BYTE *) &cf,
                          sizeof(cf),
                          WAKE_UP_INTERVAL_CAPABILITIES_REPORT_V2,
                          3 * 20,
                          n,
                          rd_cap_wake_up_callback)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  }
}

static void rd_node_probe_handle_set_wu_interval(rd_node_database_entry_t *n,
                                                 ts_param_t *p)
{
  DBG_PRINTF("/n");
  static ZW_WAKE_UP_INTERVAL_SET_V2_FRAME f;

  f.cmdClass = COMMAND_CLASS_WAKE_UP_V2;
  f.cmd      = WAKE_UP_INTERVAL_SET_V2;

  f.seconds1 = (n->wakeUp_interval >> 16) & 0xFF;
  f.seconds2 = (n->wakeUp_interval >> 8) & 0xFF;
  f.seconds3 = (n->wakeUp_interval >> 0) & 0xFF;
  f.nodeid   = ZW_GetSUCNodeID();

  ts_set_std(p, n->nodeid);
  if (!sl_zw_send_data_appl(p,
                            &f,
                            sizeof(ZW_WAKE_UP_INTERVAL_SET_V2_FRAME),
                            rd_set_wu_interval_callback,
                            n)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  }
}

static void
rd_node_probe_handle_assign_return_route(rd_node_database_entry_t *n)
{
  if (!ZW_AssignReturnRoute(n->nodeid, MyNodeID, sl_assign_route_callback)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  }
}

static void rd_node_probe_handle_probe_wu_interval(
  rd_node_database_entry_t *n,
  ts_param_t *p,
  ZW_WAKE_UP_INTERVAL_GET_FRAME *wakeup_get)
{
  ts_set_std(p, n->nodeid);

  if (!sl_zw_send_request(p,
                          (BYTE *) wakeup_get,
                          sizeof(*wakeup_get),
                          WAKE_UP_INTERVAL_REPORT,
                          SL_REQUEST_TIMEOUT_MS,
                          n,
                          rd_probe_wakeup_callback)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  }
}

static void rd_node_probe_handle_done(rd_node_database_entry_t *n)
{
  rd_ep_database_entry_t *ep;
  LOG_PRINTF("Probe of node %d is done\n", n->nodeid);
  for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
    LOG_PRINTF("Info len %i\n", ep->endpoint_info_len);
  }
  n->lastUpdate = clock_seconds();
  n->lastAwake  = clock_seconds();
  n->node_properties_flags &= ~RD_NODE_FLAG_JUST_ADDED;
  n->probe_flags = RD_NODE_FLAG_PROBE_HAS_COMPLETED;
  sli_rd_node_probe_update_complete(n);
}

static void rd_node_probe_handle_fail(rd_node_database_entry_t *n)
{
  ERR_PRINTF("Probe of node %d failed\n", n->nodeid);
  sli_rd_node_probe_update_complete(n);
}

void rd_node_probe_update(rd_node_database_entry_t *n)
{
  static ZW_MULTI_CHANNEL_END_POINT_GET_V4_FRAME multi_ep_get = {
    COMMAND_CLASS_MULTI_CHANNEL_V4,
    MULTI_CHANNEL_END_POINT_GET_V4
  };
  static ZW_MULTI_CHANNEL_END_POINT_FIND_V4_FRAME multi_ep_find = {
    COMMAND_CLASS_MULTI_CHANNEL_V4,
    MULTI_CHANNEL_END_POINT_FIND_V4,
    0xFF,
    0xFF
  };
  static ZW_WAKE_UP_INTERVAL_GET_FRAME wakeup_get = { COMMAND_CLASS_WAKE_UP,
                                                      WAKE_UP_INTERVAL_GET };
  ts_param_t p;

  if (sli_rd_node_probe_update_check(n) == FALSE) {
    return;
  }

  switch (n->state) {
    case STATUS_CREATED:
      rd_node_probe_handle_created(n);
      break;
    case STATUS_PROBE_NODE_INFO:
      rd_node_probe_handle_probe_node_info(n);
      break;
    case STATUS_PROBE_PRODUCT_ID:
      sli_rd_node_probe_update_product_id_ex(n);
      break;
    case STATUS_ENUMERATE_ENDPOINTS:
      rd_node_probe_handle_enumerate_endpoints(n, &p, &multi_ep_get);
      break;
    case STATUS_FIND_ENDPOINTS:
      rd_node_probe_handle_find_endpoints(n, &p, &multi_ep_find);
      break;
    case STATUS_PROBE_ENDPOINTS:
      break;
    case STATUS_GET_WU_CAP:
      rd_node_probe_handle_get_wu_cap(n, &p);
      break;
    case STATUS_SET_WAKE_UP_INTERVAL:
      rd_node_probe_handle_set_wu_interval(n, &p);
      break;
    case STATUS_ASSIGN_RETURN_ROUTE:
      rd_node_probe_handle_assign_return_route(n);
      break;
    case STATUS_PROBE_WAKE_UP_INTERVAL:
      rd_node_probe_handle_probe_wu_interval(n, &p, &wakeup_get);
      break;
    case STATUS_DONE:
      rd_node_probe_handle_done(n);
      break;
    case STATUS_PROBE_FAIL:
      rd_node_probe_handle_fail(n);
      break;
    default:
      break;
  }
}

static void rd_reset_probe_completed_notifier(void)
{
  memset(&node_probe_notifier, 0, sizeof(node_probe_notifier));
}

static void rd_trigger_probe_completed_notifier(rd_node_database_entry_t *node)
{
  uint8_t ii;
  for (ii = 0; ii < NUM_PROBES; ii++) {
    if (node_probe_notifier[ii].node_id == node->nodeid) {
      node_probe_notifier[ii].node_id = 0;
      if (node_probe_notifier[ii].callback) {
        node_probe_notifier[ii].callback(list_head(node->endpoints),
                                         node_probe_notifier[ii].user);
      }
    }
  }
}

static int sli_rd_register_new_node_raw(nodeid_t node,
                                        uint8_t node_properties_flags)
{
  (void) node_properties_flags; // Unused parameter
  rd_node_database_entry_t *n;
  rd_ep_database_entry_t *ep;
  n = rd_node_get_raw(node);

  sl_zw_netif_node_t *node_ip = sl_zw_netif_create_node(node);
  sl_zw_netif_add_ip6_address(node_ip);

  if (n) {
    if (n->state == STATUS_FAILING || n->state == STATUS_PROBE_FAIL
        || n->state == STATUS_DONE) {
      DBG_PRINTF("re-probing node %i old state %s\n",
                 n->nodeid,
                 rd_node_probe_state_name(n->state));
      ASSERT(n->nodeid == node);
      n->state = STATUS_CREATED;

      for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
        ep->state = EP_STATE_PROBE_INFO;
      }

      rd_node_probe_update(n);
    } else {
      WRN_PRINTF("Node probe is already in progress on node %u.\n", n->nodeid);
    }
    return TRUE;
  }
  return FALSE;
}

static void sli_rd_register_new_node(nodeid_t node,
                                     uint8_t node_properties_flags)
{
  rd_node_database_entry_t *n;
  rd_ep_database_entry_t *ep;
  n = rd_node_entry_alloc(node);

  if (!n) {
    ERR_PRINTF("Unable to register new node Out of mem!\n");
    return;
  }

  update_protocol_info(n); //Get protocol info new because this is always sync

  n->node_properties_flags |= node_properties_flags;

  /* Do not probe non-listening node since it'll most certainly fail.
   * Set it to FAIL state and probe it when this node wakes up.
   */
  if (rd_get_node_mode(n->nodeid) == MODE_NONLISTENING
      && !(n->node_properties_flags & RD_NODE_FLAG_JUST_ADDED)) {
    for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
      ep->state = EP_STATE_PROBE_INFO;
    }
    n->state = STATUS_PROBE_FAIL;
  }

  /*Endpoint 0 always exists*/
  if (!rd_add_endpoint(n, 0)) {
    n->state = STATUS_PROBE_FAIL;
    rd_node_probe_update(n);
  } else {
    /* Since status is CREATED, this node will be set as
     * current_probe_entry if there is not already a current.  This
     * ensures that if the probe machine is currently locked, it will
     * resume probing as soon as it is unlocked and eventually get to
     * this node.  */
    rd_node_probe_update(n);
  }
}

void rd_register_new_node(nodeid_t node, uint8_t node_properties_flags)
{
  DBG_PRINTF(" nodeid=%d 0x%02x\n", node, node_properties_flags);
  if (is_virtual_node(node)) {
    DBG_PRINTF("node %d is virtual\n", node);
    return;
  }

  ASSERT(nodemask_nodeid_is_valid(node));

  if (sli_rd_register_new_node_raw(node, node_properties_flags)) {
    return; //Node already exists, no need to register it again
  }

  sli_rd_register_new_node(node, node_properties_flags);
}

void rd_exit()
{
  rd_node_database_entry_t *n;

  sl_sleeptimer_stop_timer(&dead_node_timer);
  sl_sleeptimer_stop_timer(&nif_request_timer);

  for (nodeid_t i = 1; i <= ZW_MAX_NODES; i++) {
    n = rd_node_get_raw(i);
    if (n) {
      n->mode |= MODE_FLAGS_DELETED;
    }
  }
}

void rd_remove_node(nodeid_t node)
{
  rd_node_database_entry_t *n;

  ZW_Abort_SendRequest(node);
  if (node == 0) {
    return;
  }
  n = rd_node_get_raw(node);
  if (n == 0) {
    return;
  }

  /*
   * Abort probe if we have one in progress.
   */
  if (current_probe_entry == n) {
    current_probe_entry = 0;
  }

  DBG_PRINTF("Removing node %i %p\n", node, n);

  n->mode |= MODE_FLAGS_DELETED;
  if (n->nodeid == 0) {
    return;
  }

  /* Clear node from data storage */
  rd_node_entry_free(node);
}

static void sli_zw_rd_node_in_nvm_validate(nodeid_t i)
{
  if (i == MyNodeID) {
    return;
  }
  rd_node_database_entry_t *rd_node;

  DBG_PRINTF("Network has node %i\n", i);
  rd_node = rd_node_entry_import(i);

  if (rd_node == 0) {
    rd_register_new_node(i, 0x00);
    return;
  }
  rd_node_database_entry_t *n = rd_node_get_raw(i);
  rd_ep_database_entry_t *ep;

  if (n->state == STATUS_CREATED) {
    for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
      ep->state = EP_STATE_PROBE_INFO;
    }
    if ((rd_get_node_mode(n->nodeid) & 0xff) == MODE_NONLISTENING
        || (rd_get_node_mode(n->nodeid) & 0xff) == MODE_MAILBOX) {
      DBG_PRINTF("Node %d is sleeping. Probe it later.\n", n->nodeid);
      n->state = STATUS_PROBE_FAIL;
      /* We don't know the WUI, so make sure the node does not
       * become failing before we have a chance to interview
       * it. */
      n->wakeUp_interval = 0;
      n->probe_flags     = RD_NODE_PROBE_NEVER_STARTED;
    }
  } else if (n->state == STATUS_DONE) {
    /* Here we just fake that the nodes has been alive recently.
     * This is to prevent that the node becomes failing without
     * reason*/
    n->lastAwake = clock_seconds();
    /* Schedule all names to be re-probed. */
    n->state = STATUS_MDNS_PROBE;
    for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
      ep->state = EP_STATE_MDNS_PROBE;
    }
  }
  /* Mark wakeup nodes without fixed interval as recently
   * alive. This prevents frames queued to them from being
   * dropped too early after a gateway restart. */
  if (((n->mode & 0xff) == MODE_MAILBOX) && (n->wakeUp_interval == 0)) {
    n->lastAwake = clock_seconds();
  }
}

static void sli_read_zw_rd_node_in_nvm(nodemask_t nodelist)
{
  /* i is a nodeid */
  for (nodeid_t i = 1; i <= ZW_LR_MAX_NODE_ID_IN_NVM; i++) {
    if (nodemask_test_node(i, nodelist)) {
      sli_zw_rd_node_in_nvm_validate(i);
    }
  }
}

int rd_init(uint8_t lock)
{
  nodemask_t nodelist      = { 0 };
  uint16_t lr_nodelist_len = 0;
  uint8_t ver, capabilities, len, chip_type, chip_version;

  DWORD old_homeID;

  data_store_init();

  old_homeID = homeID;

  rd_reset_probe_completed_notifier();

  /* The returned homeID is in network byte order (big endian) */
  MemoryGetID((BYTE *) &homeID, &MyNodeID);
  LOG_PRINTF("HomeID is %lx node id %d\n", uip_htonl(homeID), MyNodeID);
  /* Make sure the virtual node mask is up to date */
  copy_virtual_nodes_mask_from_controller();

  nif_request_ep = 0;
  if (rd_probe_in_progress()) {
    ERR_PRINTF("RD re-initialized while probing node %u\n",
               current_probe_entry->nodeid);
  }
  current_probe_entry = 0;
  probe_lock          = lock;

  SerialAPI_GetInitData(&ver,
                        &capabilities,
                        &len,
                        nodelist,
                        &chip_type,
                        &chip_version);
  SerialAPI_GetLRNodeList(&lr_nodelist_len, NODEMASK_GET_LR(nodelist));
  /*Always update the entry for this node, since this is without cost, and network role
   * might have changed. */
  rd_register_new_node(MyNodeID, 0x00);

  DBG_PRINTF("Requesting probe of gw, node id %u, list node: ", MyNodeID);
  sl_print_hex_to_string(nodelist, 4);
  LOG_PRINTF("\n");

  sli_read_zw_rd_node_in_nvm(nodelist);

  rd_probe_resume();
  LOG_PRINTF("Resource directory init done\n");
  return (old_homeID != homeID);
}

void rd_full_network_discovery()
{
  nodemask_t nodelist = { 0 };
  uint8_t ver, capabilities, len, chip_type, chip_data;
  uint16_t lr_nodelist_len = 0;

  DBG_PRINTF("Re-synchronizing nodes\n");

  SerialAPI_GetInitData(&ver,
                        &capabilities,
                        &len,
                        nodelist,
                        &chip_type,
                        &chip_data);
  SerialAPI_GetLRNodeList(&lr_nodelist_len, NODEMASK_GET_LR(nodelist));

  for (nodeid_t i = 1; i <= ZW_LR_MAX_NODE_ID_IN_NVM; i++) {
    if (nodemask_test_node(i, nodelist)) {
      rd_register_new_node(i, 0x00);
    } else {
      rd_remove_node(i);
    }
  }
}

u8_t rd_probe_new_nodes()
{
  nodemask_t nodelist = { 0 };
  uint8_t ver, capabilities, len, chip_type, chip_data;
  nodeid_t k;
  uint16_t lr_nodelist_len = 0;

  DBG_PRINTF("Re-synchronizing nodes in rd_probe_new_nodes\n");

  SerialAPI_GetInitData(&ver,
                        &capabilities,
                        &len,
                        nodelist,
                        &chip_type,
                        &chip_data);
  SerialAPI_GetLRNodeList(&lr_nodelist_len, NODEMASK_GET_LR(nodelist));
  k = 0;
  for (nodeid_t i = 1; i <= ZW_LR_MAX_NODE_ID_IN_NVM; i++) {
    if (nodemask_test_node(i, nodelist)
        && !rd_node_exists(i)) {
      rd_register_new_node(i, 0x00);
      k++;
    }
  }
  return k;
}
/******************************************* Iterators ***************************************/

rd_ep_database_entry_t *rd_ep_iterator_group_begin(rd_group_entry_t *ge)
{
  (void) ge;
  return 0;
}

rd_ep_database_entry_t *rd_group_ep_iterator_next(rd_group_entry_t *ge,
                                                  rd_ep_database_entry_t *ep)
{
  (void) ge;
  (void) ep;

  return 0;
}

/**
 *
 * Supported  non Sec
 * Controlled non Sec
 * Supported Sec
 * Controlled Sec
 *
 */
int rd_ep_class_support(rd_ep_database_entry_t *ep, uint16_t cls)
{
  int bSecureClass;
  int bControlled;
  u8_t result;
  u16_t c;
  if (!ep->endpoint_info) {
    return 0;
  }

  if (ep->node->mode & MODE_FLAGS_DELETED) {
    return 0;
  }

  bSecureClass = 0;
  bControlled  = 0;
  result       = 0;

  for (nodeid_t i = 2; i < ep->endpoint_info_len; i++) {
    c = ep->endpoint_info[i];
    /*
     * F0 mark:
     * For interoperability considerations of some thermostat and controller
     * products. Check ZW Application CC spec, 4.101.1 together with table 134.
     */
    if ((c & 0xF0) == 0xF0 && (i < ep->endpoint_info_len - 1)) {
      i++;
      c = ((c & 0xFF) << 8) | ep->endpoint_info[i];
    }

    if (c == COMMAND_CLASS_SECURITY_SCHEME0_MARK) {
      bSecureClass = 1;
      bControlled  = 0;
    } else if (c == COMMAND_CLASS_MARK) {
      bControlled = 1;
    } else if (c == cls) {
      result |= 1 << ((bSecureClass << 1) | bControlled);
    }
  }
  return result;
}

/*
 * Lookup an endpoint from its service name
 * \return NULL if no endpoint is found otherwise return the ep structure.
 */
rd_ep_database_entry_t *rd_lookup_by_ep_name(const char *name,
                                             const char *location)
{
  (void) name;
  (void) location;

  return 0;
}

rd_group_entry_t *rd_lookup_group_by_name(const char *name)
{
  (void) name;
  return 0;
}

/**
 * \ingroup node_db
 * MUST be called when a node entry is no longer needed.
 */
void rd_free_node_dbe(rd_node_database_entry_t *n)
{
  if (n) {
    ASSERT(n->refCnt > 0);
    n->refCnt--;
  }
}

rd_ep_database_entry_t *rd_get_ep(nodeid_t nodeid, uint8_t epid)
{
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  rd_ep_database_entry_t *ep  = NULL;

  if (n == NULL) {
    return NULL;
  }
  for (ep = list_head(n->endpoints); ep != NULL; ep = list_item_next(ep)) {
    if (ep->endpoint_id == epid) {
      break;
    }
  }
  rd_free_node_dbe(n);
  return ep;
}

bool sleeping_node_is_in_firmware_upgrade(nodeid_t nodeid)
{
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n && (n->mode == MODE_FIRMWARE_UPGRADE)) {
    return true;
  }
  return false;
}

rd_node_mode_t rd_get_node_mode(nodeid_t nodeid)
{
  /* Choose a better default value in case lookup fails */
  rd_node_mode_t mode         = MODE_FLAGS_DELETED;
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);

  if (n) {
    mode = n->mode;
    rd_free_node_dbe(n);
  }
  return mode;
}

void rd_mark_node_deleted(nodeid_t nodeid)
{
  /* Choose a better default value in case lookup fails */
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n) {
    n->mode |= MODE_FLAGS_DELETED;
    rd_free_node_dbe(n);
  }
}

rd_node_state_t rd_get_node_state(nodeid_t nodeid)
{
  /* Choose a better default value in case lookup fails */
  rd_node_state_t state       = STATUS_PROBE_FAIL;
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n) {
    state = n->state;
    rd_free_node_dbe(n);
  }
  return state;
}

uint16_t rd_get_node_probe_flags(nodeid_t nodeid)
{
  uint16_t probe_flags        = RD_NODE_PROBE_NEVER_STARTED;
  rd_node_database_entry_t *n = rd_get_node_dbe(nodeid);
  if (n) {
    probe_flags = n->probe_flags;
    rd_free_node_dbe(n);
  }
  return probe_flags;
}

/***************Reimplementation of nodecache *****************************/
/*
 * Returns a bit field of Secure and non-secure support for given node
 */
static int sli_cmdclass_flags_supported(nodeid_t nodeid, WORD class)
{
  rd_node_database_entry_t *n;
  int rc;

  rc = 0;
  n  = rd_get_node_dbe(nodeid);
  if (n) {
    if (list_head(n->endpoints)) {
      rc = rd_ep_class_support(
        (rd_ep_database_entry_t *) list_head(n->endpoints),
        class);
    }
    rd_free_node_dbe(n);
  }

  return rc;
}

int sl_cmdclass_supported(nodeid_t nodeid, WORD class)
{
  return ((sli_cmdclass_flags_supported(nodeid, class) & SUPPORTED_NON_SEC) > 0);
}

int sl_cmdclass_secure_supported(nodeid_t nodeid, WORD class)
{
  return ((sli_cmdclass_flags_supported(nodeid, class) & SUPPORTED_SEC) > 0);
}

static int rd_ep_supports_cmd_class_nonsec(rd_ep_database_entry_t *ep,
                                           uint16_t class)
{
  return ((rd_ep_class_support(ep, class) & SUPPORTED_NON_SEC) > 0);
}

/**
 * Set node attribute flags
 */
BYTE sl_set_cache_entry_flag_masked(nodeid_t nodeid, BYTE value, BYTE mask)
{
  rd_node_database_entry_t *n;

  n = rd_get_node_dbe(nodeid);
  if (n) {
    n->security_flags = (n->security_flags & (~mask)) | value;
    rd_data_store_update(n);

    rd_free_node_dbe(n);
  } else {
    ERR_PRINTF("Attempt to set flag of non existing node = %i\n", nodeid);
  }
  return 1;
}

/**
 * Retrieve Cache entry flag
 */
BYTE sl_get_cache_entry_flag(nodeid_t nodeid)
{
  rd_node_database_entry_t *n;
  uint8_t rc;

  if (is_virtual_node(nodeid)) {
    n = rd_get_node_dbe(MyNodeID);
  } else {
    n = rd_get_node_dbe(nodeid);
    if (n) {
      if (nodeid != n->nodeid) {
        ERR_PRINTF("Attempt to get security flag from a node entry with "
                   "inconsistent node ID inside, nodeid: %d, n->nodeid: %d\n",
                   nodeid,
                   n->nodeid);
        assert(0);
      }
    }
  }

  if (!n) {
    ERR_PRINTF("sl_get_cache_entry_flag: on non existing node=%i\n", nodeid);
    return 0;
  }
  rc = n->security_flags;
  rd_free_node_dbe(n);
  return rc;
}

int isNodeController(nodeid_t nodeid)
{
  rd_node_database_entry_t *n;
  uint8_t rc = 0;

  n = rd_get_node_dbe(nodeid);
  if (n) {
    DBG_PRINTF("isNodeController(%d) type =%d\n", nodeid, n->nodeType);
    rc = (n->nodeType == BASIC_TYPE_CONTROLLER)
         || (n->nodeType == BASIC_TYPE_STATIC_CONTROLLER);
    rd_free_node_dbe(n);
  } else {
    ERR_PRINTF("isNodeController: on non existing node\n");
  }
  return rc;
}

/* Set node failing if it is in state STATUS_DONE and it is not
 * MODE_MAILBOX.  */
void rd_node_is_unreachable(nodeid_t node)
{
  rd_node_database_entry_t *n;
  n = rd_get_node_dbe(node);
  if (n) {
    /* Mailbox nodes are managed by dead_nodes_worker */
    if ((RD_NODE_MODE_VALUE_GET(n) != MODE_MAILBOX)
        && (RD_NODE_MODE_VALUE_GET(n) != MODE_FIRMWARE_UPGRADE)) {
      rd_set_failing(n, TRUE);
    }
    rd_free_node_dbe(n);
  }
}

/* Clear node failing if node is in STATUS_FAILING */
void rd_node_is_alive(nodeid_t node)
{
  rd_node_database_entry_t *n;
  n = rd_get_node_dbe(node);
  if (n) {
    n->lastAwake = clock_seconds();
    rd_set_failing(n, FALSE);
    rd_free_node_dbe(n);
  }
}

bool rd_check_nif_security_controller_flag(nodeid_t node)
{
  NODEINFO ni;
  sl_zw_get_node_proto_info(node, &ni);
  return (ni.security & ZWAVE_NODEINFO_CONTROLLER_NODE) > 0;
}
