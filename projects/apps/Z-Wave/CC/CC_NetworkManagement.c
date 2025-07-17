/* © 2019 Silicon Laboratories Inc. */

#define REPLACE_FAILED
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "sl_constants.h"
#include "sl_status.h"
#include <rsi_ble_apis.h>
#include <rsi_bt_common_apis.h>
#include <rsi_common_apis.h>

#include "lib/random.h"
#include "sl_gw_info.h"

#include "sl_common_config.h"
#include "sl_rd_types.h"
#include "sl_router_events.h"
#include "sl_common_type.h"
#include "sl_common_log.h"
#include "RD_internal.h"

#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "ZW_controller_api_ex.h"
#include "ZW_nodemask_api.h"
#include "ZW_udp_server.h"

#include "utls/zgw_nodemask.h"
#include "utls/sl_node_sec_flags.h"

#include "ip_bridge/sl_bridge.h"
#include "ip_translate/sl_zw_resource.h"
#include "threads/sl_security_layer.h"
#include "Net/ZW_udp_server.h"

#include "sl_sleeptimer.h"

#include "CC_InclusionController.h"
#include "CC_NetworkManagement.h"

#define ADD_REMOVE_TIMEOUT (240 * CLOCK_SECOND)
#define LEARN_TIMEOUT      (240 * CLOCK_SECOND)

#define NETWORK_MANAGEMENT_TIMEOUT        2 * 6000
#define SMART_START_SELF_DESTRUCT_TIMEOUT (3 * CLOCK_SECOND)
/* 240 seconds is enough for the joining node to time out S2 bootstrapping from
 * any state */
#define SMART_START_SELF_DESTRUCT_RETRY_TIMEOUT (240 * CLOCK_SECOND)

extern PROTOCOL_VERSION zw_protocol_version2;
/* Defer restarting Smart Start Add mode after including one smart start node
 * until this time has elapsed. */
#define SMART_START_MIDDLEWARE_PROBE_TIMEOUT (9 * CLOCK_SECOND)

#ifdef SECURITY_SUPPORT
#include "security_layer.h"
#include "s2_inclusion.h"
#endif

//extern void crypto_scalarmult_curve25519_base(uint8_t *q, const uint8_t *n);

static uint16_t nm_build_failed_node_list_frame(uint8_t *fnl_buf, uint8_t seq);
static uint16_t nm_build_node_list_frame(uint8_t *buf, uint8_t seq);

/* See CC.0034.01.08.11.001 */
#define NM_FAILED_NODE_REMOVE_FAIL 0x02
#define NM_FAILED_NODE_REMOVE_DONE 0x01
#define NM_FAILED_NODE_NOT_FOUND   0x00

struct node_add_smart_start_event_data {
  uint8_t *smart_start_homeID;
  uint8_t inclusion_options;
};

#define SL_CC_MANAGER_QUEUE_SIZE 10

const osThreadAttr_t sl_manager_thread_attributes = {
  .name       = "cc_manager",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 2048,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

static osMessageQueueId_t sli_manager_queue;

/****************************  Forward declarations
 * *****************************************/
static void nm_remove_node_status_update(LEARN_INFO *inf);
static void nm_add_node_status_update(LEARN_INFO *inf);
static void nm_learn_mode_status_update(LEARN_INFO *inf);
static void nm_replace_failed_node_status_update(BYTE status);
static void nm_remove_self_destruct_status(BYTE status);
static void nm_request_node_neighbor_status(BYTE status);

static void nm_send_reply(void *buf, u16_t len);
static void nm_learn_timer_expired(void);

static void nm_secure_inclusion_done(int status);
static void nm_timer_timeout_cb(void *none);

/** Disable networkUpdateStatusFlags to indicate that gw is rolling over.
 *
 *  Unlocks the probe engine.
 *
 *  Should be called after #NM_EV_MDNS_EXIT, after posting reset, to
 *  set up the response that should be sent when gw is ready again.
 */
static void nm_send_reply_net_updated(void);

void nm_buf_replace_data(unsigned char *buf, char old, char new, size_t len);
size_t nm_buf_insert_data(u8_t *dst,
                          const u8_t *src,
                          u8_t find,
                          u8_t add,
                          size_t len,
                          size_t max);

static void nm_net_post_event(nm_event_t ev, void *event_data);
static void nm_net_reset_state(BYTE dummy, void *user, TX_STATUS_TYPE *t);
static void
wait_for_middleware_probe(BYTE dummy, void *user, TX_STATUS_TYPE *t);
//static void nop_send_done(uint8_t status, void *user, TX_STATUS_TYPE *t);

/** Send a fail reply to \c (which may be different from nms.conn).
 * @param c Connection of the peer gw replies to.
 * @param pCmd Pointer to the incoming command.
 * @param bDatalen Command length.
 */
static void nm_net_return_fail(zwave_connection_t *c,
                               const ZW_APPLICATION_TX_BUFFER *pCmd,
                               BYTE bDatalen);

/** Build a reply to a NodeInfoCachedGet.
 *
 * Populate the relevant data from the RD entry \p node to a report frame \p f.
 *
 * The function does not call \ref rd_free_node_dbe().  If the \p node
 * argument is fetched with \ref rd_get_node_dbe(), it is the
 * responsibility of the caller to free it.
 *
 * \param node Pointer to the node database entry.
 * \param f Pointer to a frame buffer.
 * \return Length of the report.
 */
static int
nm_build_node_cached_report(rd_node_database_entry_t *node,
                            ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *f);

/********************************************************************************************/
/**
 * Sub-state flag for the Network Management State machine.
 *
 * NMS is processing an S2 addition (Smart-Start or simple S2).
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_S2_ADD 1
/**
 * Sub-state flag for the Network Management State machine.
 *
 * NMS is processing a proxy inclusion or proxy replace.
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_PROXY_INCLUSION 2
/**
 * Sub-state flag for the Network Management State machine.
 *
 * The ZW_LEARN_MODE_RETURN_INTERVIEW_STATUS was set on the
 * #LEARN_MODE_SET command, i.e., #LEARN_MODE_INTERVIEW_COMPLETED is
 * requested.
 * \ingroup NW_CMD_handler
 * \see Learn Mode Set Command (Network Management Basic Node Command
 * Class, version 2 and up)
 */
#define NMS_FLAG_LEARNMODE_NEW 4
/**
 * Sub-state flag for the Network Management State machine.
 *
 * The ZW_SET_LEARN_MODE_NWI was set on the #LEARN_MODE_SET command.
 *
 * \see Learn Mode Set Command (Network Management Basic Node Command
 * Class).
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_LEARNMODE_NWI 8
/**
 * Sub-state flag for the Network Management State machine.
 *
 * The ZW_SET_LEARN_MODE_NWE was set on the #LEARN_MODE_SET command.
 *
 * \see Learn Mode Set Command (Network Management Basic Node Command
 * Class).
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_LEARNMODE_NWE 0x10
/**
 * Sub-state flag for the Network Management State machine.
 *
 * After LEARN_MODE_DONE, NMS has determined that it is neither being
 * included nor excluded, so it must be processing a controller
 * replication (or controller shift).
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_CONTROLLER_REPLICATION 0x20
/**
 * Sub-state flag for the Network Management State machine.
 *
 * NMS is processing a Smart-Start addition.
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_SMART_START_INCLUSION 0x40
/**
 * Sub-state flag for the Network Management State machine.
 *
 * Inclusion and S2 inclusion have succeeded, so NMS should include
 * the DSK when sending NODE_ADD_STATUS.
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_REPORT_DSK 0x80
/**
 * Sub-state flag for the Network Management State machine.
 *
 * \ingroup NW_CMD_handler
 */
#define NMS_FLAG_CSA_INCLUSION 0x100

/**
 * Control structure for the Network Management State machine (NMS).
 * \ingroup NW_CMD_handler
 */
struct NetworkManagementState {
  BYTE class;
  BYTE cmd;
  BYTE seq;

  zwave_connection_t conn;

  BYTE addRemoveNodeTimerHandle;
  BYTE networkManagementTimer;
  BYTE txOptions;
  uint16_t waiting_for_ipv4_addr;
  /** The node nms is currently working on:
   * - The node being added in add node.
   * - The node including the zipgateway in learn mode.
   * - The node being probed to reply to a NodeInfoCachedGet.
   * - etc. */
  nodeid_t tmp_node;
  /*This buffer is global, because the node info is only present in the */
  nm_state_t state;
  /** Sub-state flags on the current state. */
  int flags;
  uint16_t buf_len;
  uint8_t count;
  uint8_t
    dsk_valid;   /**< Is the dsk in just_included_dsk valid for this inclusion.
                    Since 0 is valid, this needs a separate flag. */
  uint8_t granted_keys; /**< The keys we have granted */
  union {               // This union is just to increase the size of buf
    ZW_APPLICATION_TX_BUFFER buf;
    uint8_t raw_buf[256];
  };
  //  struct ctimer timer;
  sl_sleeptimer_timer_handle_t timer;
  sl_sleeptimer_timer_handle_t long_timer;
  /* Used for testing when middleware has finished probing a newly included
   * smart start node */
  BYTE just_included_dsk[16];
  uint32_t inclusion_flags;
};

/* This is not part of nms because it must survive nm_net_reset_state() */
static nodeid_t nm_newly_included_ss_nodeid;

/** Collect the events that are prerequisites for NMS: DHCP done,
 * bridge ready, and probe done.  When all are in, trigger
 * nm_send_reply() (which will set up nm_net_reset_state() as callback) and
 * cancel the networkManagementTimer.
 */
static BYTE networkUpdateStatusFlags;

int network_management_init_done = 0;
int delay_neighbor_update        = 0;
static void RequestNodeNeighborUpdat_callback_after_s2_inclusion(BYTE status);
/* This is a lock which prevents reactivation of Smart Start inclusion
 * until middleware_probe_timeout() has triggered */
int waiting_for_middleware_probe = FALSE;

struct NetworkManagementState nms = { 0 };

/* Ctimer used to abort s2 incl asynchronously to avoid a buffer overwrite in
 * libs2. */
//static sl_sleeptimer_timer_handle_t cancel_timer;

/* Smart start middleware probe timer */
static sl_sleeptimer_timer_handle_t ss_timer;

void sl_nm_timer_timeout_cb(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) t;
  nm_timer_timeout_cb(u);
}

const char *nm_state_name(int state)
{
  static char str[25];
  switch (state) {
    case NM_IDLE:
      return "NM_IDLE";
    case NM_WAITING_FOR_ADD:
      return "NM_WAITING_FOR_ADD";
    case NM_NODE_FOUND:
      return "NM_NODE_FOUND";
    case NM_WAIT_FOR_PROTOCOL:
      return "NM_WAIT_FOR_PROTOCOL";
    case NM_NETWORK_UPDATE:
      return "NM_NETWORK_UPDATE";
    case NM_WAITING_FOR_PROBE:
      return "NM_WAITING_FOR_PROBE";
    case NM_SET_DEFAULT:
      return "NM_SET_DEFAULT";
    case NM_LEARN_MODE:
      return "NM_LEARN_MODE";
    case NM_LEARN_MODE_STARTED:
      return "NM_LEARN_MODE_STARTED";
    case NM_WAIT_FOR_SECURE_ADD:
      return "NM_WAIT_FOR_SECURE_ADD";
    case NM_SENDING_NODE_INFO:
      return "NM_SENDING_NODE_INFO";
    case NM_WAITING_FOR_NODE_REMOVAL:
      return "NM_WAITING_FOR_NODE_REMOVAL";
    case NM_WAITING_FOR_FAIL_NODE_REMOVAL:
      return "NM_WAITING_FOR_FAIL_NODE_REMOVAL";
    case NM_WAITING_FOR_NODE_NEIGH_UPDATE:
      return "NM_WAITING_FOR_NODE_NEIGH_UPDATE";
    case NM_WAITING_FOR_RETURN_ROUTE_ASSIGN:
      return "NM_WAITING_FOR_RETURN_ROUTE_ASSIGN";
    case NM_WAITING_FOR_RETURN_ROUTE_DELETE:
      return "NM_WAITING_FOR_RETURN_ROUTE_DELETE";

    case NM_WAIT_FOR_PROBE_AFTER_ADD:
      return "NM_WAIT_FOR_PROBE_AFTER_ADD";
    case NM_WAIT_FOR_SECURE_LEARN:
      return "NM_WAIT_FOR_SECURE_LEARN";
    case NM_WAIT_FOR_MDNS:
      return "NM_WAIT_FOR_MDNS";
    case NM_WAIT_FOR_PROBE_BY_SIS:
      return "NM_WAIT_FOR_PROBE_BY_SIS";
    case NM_WAIT_FOR_OUR_PROBE:
      return "NM_WAIT_FOR_OUR_PROBE";
    case NM_WAIT_DHCP:
      return "NM_WAIT_DHCP";
    case NM_REMOVING_ASSOCIATIONS:
      return "NM_REMOVING_ASSOCIATIONS";

    case NM_REPLACE_FAILED_REQ:
      return "NM_REPLACE_FAILED_REQ";
    case NM_PREPARE_SUC_INCLISION:
      return "NM_PREPARE_SUC_INCLISION";
    case NM_WIAT_FOR_SUC_INCLUSION:
      return "NM_WIAT_FOR_SUC_INCLUSION";
    case NM_PROXY_INCLUSION_WAIT_NIF:
      return "NM_PROXY_INCLUSION_WAIT_NIF";
    case NM_WAIT_FOR_SELF_DESTRUCT:
      return "NM_WAIT_FOR_SELF_DESTRUCT";
    case NM_WAIT_FOR_SELF_DESTRUCT_RETRY:
      return "NM_WAIT_FOR_SELF_DESTRUCT_RETRY";
    case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT:
      return "NM_WAIT_FOR_TX_TO_SELF_DESTRUCT";
    case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY:
      return "NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY";
    case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL:
      return "NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL";
    case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY:
      return "NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY";
    case NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD:
      return "NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD";
    default:
      sprintf(str, "%d", state);
      return str;
  }
}

const char *nm_event_name(int ev)
{
  static char str[25];
  switch (ev) {
    case NM_EV_ADD_LEARN_READY:
      return "NM_EV_ADD_LEARN_READY";
    case NM_EV_ADD_NODE_FOUND:
      return "NM_EV_ADD_NODE_FOUND";
    case NM_EV_ADD_CONTROLLER:
      return "NM_EV_ADD_CONTROOLER";
    case NM_EV_ADD_PROTOCOL_DONE:
      return "NM_EV_ADD_PROTOCOL_DONE";
    case NM_EV_ADD_END_NODE:
      return "NM_EV_ADD_END_NODE";
    case NM_EV_ADD_FAILED:
      return "NM_EV_ADD_FAILED";
    case NM_EV_ADD_NOT_PRIMARY:
      return "NM_EV_ADD_NOT_PRIMARY";
    case NM_EV_ADD_NODE_STATUS_DONE:
      return "NM_EV_ADD_NODE_STATUS_DONE";

    case NM_EV_NODE_ADD:
      return "NM_EV_NODE_ADD";
    case NM_NODE_ADD_STOP:
      return "NM_NODE_ADD_STOP";
    case NM_EV_TIMEOUT:
      return "NM_EV_TIMEOUT";
    case NM_EV_SECURITY_DONE:
      return "NM_EV_SECURITY_DONE";
    case NM_EV_ADD_SECURITY_REQ_KEYS:
      return "NM_EV_ADD_SECURITY_REQ_KEYS";
    case NM_EV_ADD_SECURITY_KEY_CHALLENGE:
      return "NM_EV_ADD_SECURITY_KEY_CHALLANGE";
    case NM_EV_NODE_PROBE_DONE:
      return "NM_EV_NODE_PROBE_DONE";
    case NM_EV_DHCP_DONE:
      return "NM_EV_DHCP_DONE";
    case NM_EV_NODE_ADD_S2:
      return "NM_EV_NODE_ADD_S2";

    case NM_EV_ADD_SECURITY_KEYS_SET:
      return "NM_EV_ADD_SECURITY_KEYS_SET";
    case NM_EV_ADD_SECURITY_DSK_SET:
      return "NM_EV_ADD_SECURITY_DSK_SET";

    case NM_EV_REPLACE_FAILED_START:
      return "NM_EV_REPLACE_FAILED_START";

    case NM_EV_REPLACE_FAILED_STOP:
      return "NM_EV_REPLACE_FAILED_STOP";
    case NM_EV_REPLACE_FAILED_DONE:
      return "NM_EV_REPLACE_FAILED_DONE";
    case NM_EV_REPLACE_FAILED_FAIL:
      return "NM_EV_REPLACE_FAILED_FAIL";
    case NM_EV_REPLACE_FAILED_START_S2:
      return "NM_EV_REPLACE_FAILED_START_S2";
    case NM_EV_MDNS_EXIT:
      return "NM_EV_MDNS_EXIT";
    case NM_EV_LEARN_SET:
      return "NM_EV_LEARN_SET";
    case NM_EV_REQUEST_NODE_LIST:
      return "NM_EV_REQUEST_NODE_LIST";
    case NM_EV_REQUEST_FAILED_NODE_LIST:
      return "NM_EV_REQUEST_FAILED_NODE_LIST";
    case NM_EV_PROXY_COMPLETE:
      return "NM_EV_PROXY_COMPLETE";
    case NM_EV_START_PROXY_INCLUSION:
      return "NM_EV_START_PROXY_INCLUSION";
    case NM_EV_START_PROXY_REPLACE:
      return "NM_EV_START_PROXY_REPLACE";
    case NM_EV_NODE_INFO:
      return "NM_EV_NODE_INFO";
    case NM_EV_FRAME_RECEIVED:
      return "NM_EV_FRAME_RECEIVED";
    case NM_EV_ALL_PROBED:
      return "NM_EV_ALL_PROBED";
    case NM_EV_NODE_ADD_SMART_START:
      return "NM_EV_NODE_ADD_SMART_START";
    case NM_EV_TX_DONE_SELF_DESTRUCT:
      return "NM_EV_TX_DONE_SELF_DESTRUCT";
    case NM_EV_REMOVE_FAILED_OK:
      return "NM_EV_REMOVE_FAILED_OK";
    case NM_EV_REMOVE_FAILED_FAIL:
      return "NM_EV_REMOVE_FAILED_FAIL";
    case NM_EV_ADD_NODE_STATUS_SFLND_DONE:
      return "NM_EV_ADD_NODE_STATUS_SFLND_DONE";
    case NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE:
      return "NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE";

    default:
      sprintf(str, "%d", ev);
      return str;
  }
}

/**
 * Integer log2
 */
static unsigned int ilog2(int x)
{
  int i = 16;
  do {
    i--;
    if ((1 << i) & x) {
      return i;
    }
  } while (i);
  return 0;
}

static clock_time_t ageToTime(BYTE age)
{
  return (1 << (age & 0xf)) * 60;
}

// Ring Patch
int should_skip_flirs_nodes_be_used()
{
  /* ADD_NODE_OPTION_SFLND is only available in 7.19.0 or above.
   * Use it when the expected inclusion time will be more than 60 seconds.
   */
  LOG_PRINTF("Protocol version %d.%d\n",
             zw_protocol_version2.protocolVersionMajor,
             zw_protocol_version2.protocolVersionMinor);
  return (zw_protocol_version2.protocolVersionMajor > 7
          || (zw_protocol_version2.protocolVersionMajor == 7
              && zw_protocol_version2.protocolVersionMinor >= 19));
}

static uint8_t is_cc_in_nif(uint8_t *nif, uint8_t nif_len, uint8_t cc)
{
  int i;
  for (i = 0; i < nif_len; i++) {
    if (nif[i] == cc) {
      return 1;
    }
  }
  return 0;
}

static void inclusion_controller_complete(int status)
{
  nm_net_post_event(NM_EV_PROXY_COMPLETE, &status);
}

static uint8_t set_unsolicited_as_nm_dest()
{
  nms.seq = random_rand() & 0xFF;
  return TRUE;
}

/** Reset the fields in the global NM state to be ready for a node add. */
static void nm_prepare_for_node_add_status(bool lr)
{
  memset(&nms.buf, 0, sizeof(nms.buf));
  nms.cmd = NODE_ADD;
  nms.buf.ZW_NodeAddStatus1byteFrame.cmdClass =
    COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME *f =
    (ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME *) &nms.buf;
  if (lr) {
    f->cmd      = EXTENDED_NODE_ADD_STATUS;
    nms.buf_len = f->nodeInfoLength + 6;
  } else {
    nms.buf.ZW_NodeAddStatus1byteFrame.cmd = NODE_ADD_STATUS;
    nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
  }
  nms.buf.ZW_NodeAddStatus1byteFrame.seqNo          = nms.seq;
  nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
}

static void nm_net_scheme_clear(nm_event_t ev)
{
  if ((nms.state == NM_LEARN_MODE_STARTED)
      || (nms.state == NM_WAIT_FOR_SECURE_LEARN)) {
    if (ev == NM_EV_S0_STARTED) {
      /* Make temporary NIF, used for inclusion */
      LOG_PRINTF("S0 inclusion has started. Setting net_scheme to S0\n");
    }
  }
}

static void nm_net_learn_set(nm_event_t ev, void *event_data)
{
  (void)ev;

  ZW_LEARN_MODE_SET_FRAME *f = (ZW_LEARN_MODE_SET_FRAME *) event_data;

  if (f->mode == ZW_SET_LEARN_MODE_CLASSIC) {
    nms.flags = 0;
  } else if (f->mode == ZW_SET_LEARN_MODE_NWI) {
    nms.flags = NMS_FLAG_LEARNMODE_NWI;
    /*Note it is supposed to be MODE CLASSIC*/
  } else if (f->mode == ZW_SET_LEARN_MODE_NWE) {
    nms.flags = NMS_FLAG_LEARNMODE_NWE;
  } else if (f->mode == ZW_SET_LEARN_MODE_DISABLE) {
    ZW_LEARN_MODE_SET_STATUS_FRAME *f =
      (ZW_LEARN_MODE_SET_STATUS_FRAME *) &nms.buf;
    f->cmdClass  = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
    f->cmd       = LEARN_MODE_SET_STATUS;
    f->seqNo     = nms.seq;
    f->status    = LEARN_MODE_FAILED;
    f->newNodeId = 0;
    f->reserved  = 0;
    nm_send_reply(f, sizeof(ZW_LEARN_MODE_SET_STATUS_FRAME));
    return;
  } else {
    LOG_PRINTF("Unknown learnmode\n");
    return;
  }

  /* This clears the DSK field in Learn Mode Set Status to make it more
   * obvious that it is not valid during exclusion or replication. Even
   * though we know that all-zeros is also a valid (but unlikely) DSK. */
  memset(&nms.buf, 0, sizeof(nms.buf));

  /* Before going to learn mode, Stop add node mode in case we are in Smart
   * Start add mode */
  ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);

  nms.state = NM_LEARN_MODE;

  if ((f->reserved & ZW_LEARN_MODE_RETURN_INTERVIEW_STATUS)
      && (nms.flags != NMS_FLAG_LEARNMODE_NWE)) {
    nms.flags |= NMS_FLAG_LEARNMODE_NEW;
  }

  nms.count = 0;
  ZW_SetLearnMode(ZW_SET_LEARN_MODE_CLASSIC, nm_learn_mode_status_update);

  if (f->mode == ZW_SET_LEARN_MODE_CLASSIC) {
    /* Keep the nm_timer_timeout_cb to 20 seconds if the learn mode is CLASSIC */
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 20,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
  } else {
    /* in case of NWI mode Keeping the nm_timer_timeout_cb to 2 seconds come from
           recommendation in the document SDS11846. We keep the nm_timer_timeout_cb same for
           both NWI and NEW modes*/
    /* This nm_timer_timeout_cb has been extended from 2 seconds, because we changed too
     * early and broke direct range exclusion. */
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 6,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
  }
}

static void nm_net_node_add_smart_start(nm_event_t ev, void *event_data)
{
  (void)ev;
  (void)event_data;

  LOG_PRINTF("Dont support\n");
}

static void nm_net_send_reply_to_zip(nm_event_t ev, void *event_data)
{
  (void)ev;
  (void)event_data;

  //  nms.state = NM_IDLE; Reset state will sent the FSM to IDLE
  if (nms.flags & NMS_FLAG_PROXY_INCLUSION) {
    if (nms.buf.ZW_NodeAddStatus1byteFrame.status == ADD_NODE_STATUS_DONE) {
      inclusion_controller_send_report(INCLUSION_CONTROLLER_STEP_OK);
    } else {
      inclusion_controller_send_report(INCLUSION_CONTROLLER_STEP_FAILED);
    }
  }
  /* Smart Start inclusion requires an extra step after sending reply */
  if (!(nms.flags & NMS_FLAG_SMART_START_INCLUSION)) {
    nm_send_reply(&nms.buf, nms.buf_len);
  } else {
    if (nms.buf.ZW_NodeAddStatus1byteFrame.status == ADD_NODE_STATUS_FAILED) {
      LOG_PRINTF(
        "Sending network management reply: Smart Start inclusion failed\n");
    } else {
      LOG_PRINTF(
        "Sending network management reply: Smart Start inclusion success\n");
    }
    // DS
#if UNSOC_SUPPORTED
    send_to_both_unsoc_dest((uint8_t *) &nms.buf,
                            nms.buf_len,
                            wait_for_middleware_probe);
#endif
    sl_zw_send_zip_data(&nms.conn,
                        (BYTE *) &nms.buf,
                        nms.buf_len,
                        wait_for_middleware_probe);
  }
}

static void nm_net_node_replace_fail(nm_event_t ev, void *event_data)
{
  ZW_FAILED_NODE_REPLACE_FRAME *f = (ZW_FAILED_NODE_REPLACE_FRAME *) event_data;
  ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *reply =
    (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *) &nms.buf;
  nms.cmd      = FAILED_NODE_REPLACE;
  nms.tmp_node = f->nodeId;
  nms.state    = NM_REPLACE_FAILED_REQ;

  reply->cmdClass    = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  reply->cmd         = FAILED_NODE_REPLACE_STATUS;
  reply->seqNo       = nms.seq;
  reply->nodeId      = nms.tmp_node;
  reply->status      = ZW_FAILED_NODE_REPLACE_FAILED;
  reply->kexFailType = 0x00;
  reply->grantedKeys = 0x00;
  nms.buf_len        = sizeof(ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX);

  if (ev == NM_EV_REPLACE_FAILED_START_S2) {
    nms.flags = NMS_FLAG_S2_ADD;
  }
  if (ev == NM_EV_REPLACE_FAILED_STOP) {
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  }

  ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);

  LOG_PRINTF("Replace failed, node %i \n", f->nodeId);
  if (ZW_ReplaceFailedNode(f->nodeId,
                           f->txOptions != TRANSMIT_OPTION_LOW_POWER,
                           nm_replace_failed_node_status_update)
      == ZW_FAILED_NODE_REMOVE_STARTED) {
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 ADD_REMOVE_TIMEOUT * 10,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
  } else {
    LOG_PRINTF("replace failed not started\n");
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  }
}

static void nm_net_idle_state_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_LEARN_SET) {
    nm_net_learn_set(ev, event_data);
  } else if (ev == NM_EV_NODE_ADD || ev == NM_EV_NODE_ADD_S2) {
    ZW_AddNodeToNetwork(*((uint8_t *) event_data), nm_add_node_status_update);
    nms.state = NM_WAITING_FOR_ADD;

    nm_prepare_for_node_add_status(false);
    nms.dsk_valid = FALSE;

    if (ev == NM_EV_NODE_ADD_S2) {
      nms.flags = NMS_FLAG_S2_ADD;
    }
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 ADD_REMOVE_TIMEOUT * 10,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
  } else if (ev == NM_EV_NODE_ADD_SMART_START) {
    nm_net_node_add_smart_start(ev, event_data);
  } else if (ev == NM_EV_NODE_ADD_SMART_START_REJECT) {
    nm_net_reset_state(0, 0, 0);
  } else if (ev == NM_EV_REPLACE_FAILED_START
             || ev == NM_EV_REPLACE_FAILED_START_S2
             || ev == NM_EV_REPLACE_FAILED_STOP) {
    nm_net_node_replace_fail(ev, event_data);
  } else if (ev == NM_EV_REQUEST_NODE_LIST) {
    /*I'm the SUC/SIS or i don't know the SUC/SIS*/
    uint8_t buf[sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME)
                + sizeof(nodemask_t)];
    uint16_t len = nm_build_node_list_frame((uint8_t *) buf, nms.seq);
    nm_send_reply(&buf, len);
  } else if (ev == NM_EV_REQUEST_FAILED_NODE_LIST) {
    uint8_t buf[sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME)
                + sizeof(nodemask_t)];
    uint16_t len = nm_build_failed_node_list_frame((uint8_t *) buf, nms.seq);
    nm_send_reply(&buf, len);
  } else if (ev == NM_NODE_ADD_STOP) {
    LOG_PRINTF("Event  NM_NODE_ADD_STOP in NM_IDLE state\n");
    memset(&nms.buf, 0, sizeof(nms.buf.ZW_NodeAddStatus1byteFrame));
    nms.buf.ZW_NodeAddStatus1byteFrame.cmdClass =
      COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
    nms.buf.ZW_NodeAddStatus1byteFrame.cmd    = NODE_ADD_STATUS;
    nms.buf.ZW_NodeAddStatus1byteFrame.seqNo  = nms.seq;
    nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
    nms.buf_len = sizeof(nms.buf.ZW_NodeAddStatus1byteFrame) - 1;
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  } else if (ev == NM_EV_START_PROXY_INCLUSION
             || ev == NM_EV_START_PROXY_REPLACE) {
    nms.tmp_node = *((uint16_t *) event_data);
    nms.cmd =
      (ev == NM_EV_START_PROXY_INCLUSION) ? NODE_ADD : FAILED_NODE_REPLACE;

    ZW_RequestNodeInfo(nms.tmp_node, 0);
    nms.state = NM_PROXY_INCLUSION_WAIT_NIF;
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 5,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);

    /* Send inclusion request to unsolicited destination */
    set_unsolicited_as_nm_dest();
  }
}

static void nm_net_replace_failed_req_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_TIMEOUT || ev == NM_EV_REPLACE_FAILED_STOP
      || ev == NM_EV_REPLACE_FAILED_FAIL) {
    ZW_AddNodeToNetwork(ADD_NODE_STOP, 0);
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  }

  if (ev != NM_EV_REPLACE_FAILED_DONE) {
    return;
  }

  uint32_t zero    = 0;
  int common_flags = 0;
  nms.state        = NM_WAIT_FOR_SECURE_ADD;

  /*Cache security flags*/
  if (nms.flags & NMS_FLAG_PROXY_INCLUSION) {
    LEARN_INFO *inf = (LEARN_INFO *) event_data;
    if (NULL != inf) {
      uint8_t *nif = inf->pCmd + 3;
      if (is_cc_in_nif(nif, inf->bLen - 3, COMMAND_CLASS_SECURITY)) {
        common_flags |= NODE_FLAG_SECURITY0;
      }
    }
  } else {
    common_flags = sl_get_cache_entry_flag(nms.tmp_node);
  }

  sl_appl_controller_update(UPDATE_STATE_DELETE_DONE,
                            nms.tmp_node,
                            0,
                            0,
                            NULL);

  rd_probe_lock(TRUE);

  nodeid_t suc_node = ZW_GetSUCNodeID();

  if (suc_node != MyNodeID
      && sl_cmdclass_supported(suc_node, COMMAND_CLASS_INCLUSION_CONTROLLER)) {
    rd_register_new_node(nms.tmp_node, RD_NODE_FLAG_JUST_ADDED);
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 2,
                                 sl_nm_timer_timeout_cb,
                                 0,
                                 1,
                                 0);
    nms.state = NM_PREPARE_SUC_INCLISION;
    return;
  }

  rd_register_new_node(nms.tmp_node,
                       RD_NODE_FLAG_JUST_ADDED | RD_NODE_FLAG_ADDED_BY_ME);

  if (common_flags & NODE_FLAG_SECURITY0) {
    if (nms.flags & NMS_FLAG_PROXY_INCLUSION) {
      inclusion_controller_you_do_it(nm_secure_inclusion_done);
      return;
    } else if (!(nms.flags & NMS_FLAG_SMART_START_INCLUSION)) { /*
                                                                    SmartStart inclusions must never be downgraded to S0 */
      security_add_begin(nms.tmp_node,
                         nms.txOptions,
                         isNodeController(nms.tmp_node),
                         nm_secure_inclusion_done);
      return;
    }
  }

  /*This is a non secure node or the node has already been included
   * securely*/
  ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *reply =
    (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *) &nms.buf;
  reply->status = ZW_FAILED_NODE_REPLACE_DONE;

  nm_net_post_event(NM_EV_SECURITY_DONE, &zero);
}

static void nm_net_node_found_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_ADD_CONTROLLER || ev == NM_EV_ADD_END_NODE) {
    LEARN_INFO *inf = (LEARN_INFO *) event_data;
    clock_time_t tout;
    if (ev == NM_EV_ADD_CONTROLLER) {
      tout = 256 * 1000;
    } else if (ev == NM_EV_ADD_END_NODE) {
      tout = 256 * 1000;
    }

    if (inf->bLen && (inf->bSource != 0)) {
      sl_sleeptimer_start_timer_ms(&nms.timer,
                                   tout,
                                   sl_nm_timer_timeout_cb,
                                   NULL,
                                   1,
                                   0);
      nms.tmp_node = inf->bSource;

      ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME *f =
        (ZW_EXTENDED_NODE_ADD_STATUS_1BYTE_FRAME *) &nms.buf;
      if ((nms.flags & NMS_FLAG_SMART_START_INCLUSION)
          && (is_lr_node(nms.tmp_node))) {
        f->newNodeIdMSB = inf->bSource >> 8;
        f->newNodeIdLSB = inf->bSource & 0xFF;
      } else {
        nms.buf.ZW_NodeAddStatus1byteFrame.newNodeId = inf->bSource;
      }
      nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = inf->bLen + 3;
      memcpy(&(nms.buf.ZW_NodeAddStatus1byteFrame.basicDeviceClass),
             inf->pCmd,
             inf->bLen);
      nms.state = NM_WAIT_FOR_PROTOCOL;
    } else {
      if (is_virtual_node(inf->bSource)) {
        LOG_PRINTF("Node id included was a virtual node id: %d\n",
                   inf->bSource);
      }
      nm_net_post_event(NM_EV_ADD_FAILED, NULL);
    }
    if (nms.flags & NMS_FLAG_SMART_START_INCLUSION) {
      nm_newly_included_ss_nodeid = nms.tmp_node;
    }
  } else if (ev == NM_EV_ADD_FAILED || ev == NM_EV_TIMEOUT) {
    nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;

    /* Add node failed - Application should indicate this to user */
    ZW_AddNodeToNetwork(ADD_NODE_STOP_FAILED, NULL);

    rd_probe_lock(FALSE); // Unlock the probe machine
    nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
    nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  }
}

static void sli_nm_net_wait_add_node_done_handler(nm_event_t ev,
                                                  void *event_data)
{
  (void)ev;
  (void)event_data;

  NODEINFO ni;

  /* It is recommended to stop the process again here */
  ZW_AddNodeToNetwork(ADD_NODE_STOP, NULL);
  /* Reset the timer started in NM_NODE_FOUND state after calculating
   * nm_timer_timeout_cb with rd_calculate_inclusion_timeout()
   * Reset to value more than S2 inclusion TAI1 to allow user to change
   * granted keys */
  sl_sleeptimer_start_timer_ms(&nms.timer,
                               CLOCK_SECOND * 250,
                               sl_nm_timer_timeout_cb,
                               NULL,
                               1,
                               0);

  /* Get the Capabilities and Security fields. */
  sl_zw_get_node_proto_info(nms.tmp_node, &ni);
  nms.buf.ZW_NodeAddStatus1byteFrame.properties1 = ni.capability;
  nms.buf.ZW_NodeAddStatus1byteFrame.properties2 = ni.security;

  nms.state = NM_WAIT_FOR_SECURE_ADD;

  if ((rd_node_exists(nms.tmp_node))
      && !(nms.flags & NMS_FLAG_PROXY_INCLUSION)) {
    uint32_t flags = sl_get_cache_entry_flag(nms.tmp_node);
    LOG_PRINTF("This node has already been included\n");
    /*This node has already been included*/
    nms.dsk_valid = FALSE;
    nm_net_post_event(NM_EV_SECURITY_DONE, &flags);

    /* In NM_WAIT_FOR_SECURE_ADD we go straight to
     * NM_WAIT_FOR_PROBE_AFTER_ADD, so here we set up the trigger to
     * continue from that state. */
    /* Do we want to check things like probe state or deleted flag here? */
    return;
  }

  nodeid_t suc_node = ZW_GetSUCNodeID();

  rd_probe_lock(TRUE);

  if (suc_node != MyNodeID
      && sl_cmdclass_supported(suc_node, COMMAND_CLASS_INCLUSION_CONTROLLER)) {
    rd_register_new_node(nms.tmp_node, RD_NODE_FLAG_JUST_ADDED);
    sl_appl_controller_update(UPDATE_STATE_NEW_ID_ASSIGNED,
                              nms.tmp_node,
                              0,
                              0,
                              NULL);
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 2,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
    nms.state = NM_PREPARE_SUC_INCLISION;
    return;
  }

  rd_register_new_node(nms.tmp_node,
                       RD_NODE_FLAG_JUST_ADDED | RD_NODE_FLAG_ADDED_BY_ME);
  sl_appl_controller_update(UPDATE_STATE_NEW_ID_ASSIGNED,
                            nms.tmp_node,
                            0,
                            0,
                            NULL);

  if (sl_get_cache_entry_flag(MyNodeID) & (NODE_FLAG_SECURITY0)) {
    /*Security 0 inclusion*/
    if (is_cc_in_nif(&nms.buf.ZW_NodeAddStatus1byteFrame.commandClass1,
                     nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength - 3,
                     COMMAND_CLASS_SECURITY)) {
      if (nms.flags & NMS_FLAG_PROXY_INCLUSION) {
        inclusion_controller_you_do_it(nm_secure_inclusion_done);
      } else if (!(nms.flags & NMS_FLAG_SMART_START_INCLUSION)) { /* SmartStart
                                                                     inclusions must
                                                                     never be
                                                                     downgraded to S0
                                                                   */
        security_add_begin(nms.tmp_node,
                           nms.txOptions,
                           isNodeController(nms.tmp_node),
                           nm_secure_inclusion_done);
      }
      return;
    }
  }

  /* This is a non secure node or a non secure GW */
  uint8_t zero = 0;
  nm_net_post_event(NM_EV_SECURITY_DONE, &zero);
}

static void nm_net_wait_protocol_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_ADD_PROTOCOL_DONE) {
    ZW_AddNodeToNetwork(ADD_NODE_STOP, nm_add_node_status_update);
  } else if (ev == NM_EV_ADD_NODE_STATUS_SFLND_DONE) {
    LOG_PRINTF(
      "The module has skipped doing Neighbor Discovery of FLIRS nodes\n");
    delay_neighbor_update = 1;
  } else if ((ev == NM_EV_ADD_NODE_STATUS_DONE)) {
    sli_nm_net_wait_add_node_done_handler(ev, event_data);
  } else if (ev == NM_EV_TIMEOUT || ev == NM_EV_ADD_FAILED
             || ev == NM_EV_ADD_NOT_PRIMARY) {
    nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;

    /* Add node failed - Application should indicate this to user */
    ZW_AddNodeToNetwork(ADD_NODE_STOP_FAILED, NULL);

    rd_probe_lock(FALSE); // Unlock the probe machine
    nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
    nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  }
}

static void sli_nm_net_wait_secure_done_handler(nm_event_t ev, void *event_data)
{
  uint32_t inclusion_flags;

  sl_sleeptimer_stop_timer(&nms.timer);
  if (ev == NM_NODE_ADD_STOP) {
    inclusion_flags = NODE_FLAG_KNOWN_BAD;
  } else {
    inclusion_flags     = (*(uint32_t *) event_data);
    nms.inclusion_flags = inclusion_flags;
  }

  /*If status has not yet already been set use the result of the secure
   * add*/
  if (nms.cmd == NODE_ADD) {
    nms.buf.ZW_NodeAddStatus1byteFrame.status =
      inclusion_flags & NODE_FLAG_KNOWN_BAD ? ADD_NODE_STATUS_SECURITY_FAILED
      : ADD_NODE_STATUS_DONE;
  } else {
    ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *reply =
      (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *) &nms.buf;

    reply->status      = inclusion_flags & NODE_FLAG_KNOWN_BAD
                         ? ADD_NODE_STATUS_SECURITY_FAILED
                         : ZW_FAILED_NODE_REPLACE_DONE;
    reply->kexFailType = (inclusion_flags >> 16) & 0xFF; // TODO
  }
  sl_set_cache_entry_flag_masked(nms.tmp_node,
                                 inclusion_flags & 0xFF,
                                 NODE_FLAGS_SECURITY);

  /* If this is a failed smart start inclusion, dont do probing. The node
   * will self destruct soon anyway. */
  if ((nms.flags & NMS_FLAG_SMART_START_INCLUSION)
      && (inclusion_flags & NODE_FLAG_KNOWN_BAD)) {
    nms.state = NM_WAIT_FOR_SELF_DESTRUCT;
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 SMART_START_SELF_DESTRUCT_TIMEOUT,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
    return;
  }

  if (nms.dsk_valid == TRUE) {
    /* If the DSK is S2 confirmed, store it in the RD.  Smart
     * start devices must be S2 by now, S2 devices need an extra
     * check on inclusion_flags.  S0 and non-secure devices should
     * not have a DSK.  */
    /* if nms.flags have  NMS_FLAG_S2_ADD, that covers both smartstart and
     * original s2 */
    if ((nms.flags & NMS_FLAG_SMART_START_INCLUSION)
        || ((inclusion_flags & NODE_FLAGS_SECURITY2)
            && (!(inclusion_flags & NODE_FLAG_KNOWN_BAD)))) {
      /* Link the PVL entry to the RD entry: Add the DSK to the
       * RD and copy name and location tlvs if they exist */
      rd_node_add_dsk(nms.tmp_node, 16, nms.just_included_dsk);
    }
  }

  /* If this secure inclusion was successful and S2, remember we should
   * report the DSK when sending NODE_ADD_STATUS later */
  if ((inclusion_flags & NODE_FLAGS_SECURITY2)
      && !(inclusion_flags & NODE_FLAG_KNOWN_BAD)) {
    nms.flags |= NMS_FLAG_REPORT_DSK;
  }

  if (nms.flags & NMS_FLAG_SMART_START_INCLUSION) {
    /* We are also setting this flag in state NM_WAIT_DHCP where we also
     * start the timer responsible for clearing the flag eventually. We have
     * to set the flag this early because the probing process will throw
     * ZIP_EVENT_ALL_NODES_PROBED calling _init_if_pending(), which we need
     * to block.
     */
    waiting_for_middleware_probe = TRUE;
  }

  if (delay_neighbor_update) {
    LOG_PRINTF("Starting the delayed Neighbor Discovery for node:%d now. "
               "FLIRS nodes will also be discovered\n",
               nms.tmp_node);
    nms.state         = NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD;
    clock_time_t tout = rd_calculate_inclusion_timeout(TRUE);
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 tout,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
    ZW_RequestNodeNeighborUpdate(
      nms.tmp_node,
      RequestNodeNeighborUpdat_callback_after_s2_inclusion);
    delay_neighbor_update = 0;
  } else {
    /* Now we either have smart start with S2 success or plain S2 or node
     * already exists. */
    nms.state = NM_WAIT_FOR_PROBE_AFTER_ADD;
    /* Re-interview this node as it now has secure classes and its nif might
     * have changed.*/
    /* When the interview is completed, ZIP_Router will trigger NMS
     * again by calling NetworkManagement_node_probed(). */
    rd_probe_lock(FALSE);
  }
}

static void nm_net_wait_secure_add_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_SECURITY_DONE || ev == NM_NODE_ADD_STOP) {
    sli_nm_net_wait_secure_done_handler(ev, event_data);
  } else if (ev == NM_EV_ADD_SECURITY_REQ_KEYS) {
    LOG_PRINTF("S2 Not supported\n");
  } else if (ev == NM_EV_ADD_SECURITY_KEYS_SET) {
    ZW_NODE_ADD_KEYS_SET_FRAME_EX *f =
      (ZW_NODE_ADD_KEYS_SET_FRAME_EX *) event_data;

    if (f->reserved_accept & NODE_ADD_KEYS_SET_EX_CSA_BIT) {
      nms.flags |= NMS_FLAG_CSA_INCLUSION;
    }
  } else if (ev == NM_EV_ADD_SECURITY_DSK_SET) {
    ZW_NODE_ADD_DSK_SET_FRAME_EX *f =
      (ZW_NODE_ADD_DSK_SET_FRAME_EX *) event_data;
    uint8_t dsk_len = f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_DSK_LEN_MASK;

    LOG_PRINTF("DSK accept bit %u, dsk len %u\n",
               f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_EX_ACCEPT_BIT,
               f->accet_reserved_dsk_len & NODE_ADD_DSK_SET_DSK_LEN_MASK);

    if (dsk_len <= 16) {
      nms.dsk_valid = TRUE;
      memcpy(nms.just_included_dsk, f->dsk, dsk_len);
    } else {
    }
  } else if (ev == NM_EV_TIMEOUT) {
    nm_net_post_event(NM_NODE_ADD_STOP, 0);
  }
}

static void sli_nm_net_wait_probe_add_status(void)
{
  if (nms.flags & NMS_FLAG_REPORT_DSK) {
    /* Check if there is enough space for 1 byte dsk length and dsk itself
     */
    if (nms.buf_len
        > (sizeof(nms.raw_buf) - (1 + sizeof(nms.just_included_dsk)))) {
      LOG_PRINTF("Copying the DSK length and DSK at wrong offset. "
                 "Correcting.\n");
      nms.buf_len = (sizeof(nms.raw_buf) - (1 + sizeof(nms.just_included_dsk)));
      assert(0);
    }
    /* Add node DSK to add node callback*/
    (((uint8_t *) &nms.buf)[nms.buf_len++]) = sizeof(nms.just_included_dsk);
    memcpy(&(((uint8_t *) &nms.buf)[nms.buf_len]),
           nms.just_included_dsk,
           sizeof(nms.just_included_dsk));
    nms.buf_len += sizeof(nms.just_included_dsk);
  } else {
    /* report 0-length DSK */
    uint8_t *nmsbuf     = (uint8_t *) (&(nms.buf));
    nmsbuf[nms.buf_len] = 0;
    nms.buf_len++;
  }
}

static void sl_nm_net_wait_probe_after_add_node(rd_node_database_entry_t *ndbe)
{
  rd_ep_database_entry_t *ep = rd_ep_first(ndbe->nodeid);
  ZW_NODE_ADD_STATUS_1BYTE_FRAME *r =
    (ZW_NODE_ADD_STATUS_1BYTE_FRAME *) &nms.buf;
  int len;
  if (ep->state == EP_STATE_PROBE_DONE) {
    /*
     * Here we send status back to LAN side. Options can be:
     * 1. Node Add Status for classic node (SmartStart or not)
     * 2. Extended Node Add Status for LR SmartStart node
     *
     * Both ZW frame format are the same until KEX_FAIL so
     * ZW_NODE_ADD_STATUS_1BYTE_FRAME can be used for both.
     */
    r->basicDeviceClass    = ndbe->nodeType;
    r->genericDeviceClass  = ep->endpoint_info[0];
    r->specificDeviceClass = ep->endpoint_info[1];
    int max;

    if ((nms.flags & NMS_FLAG_REPORT_DSK)
        && (nms.buf.ZW_NodeAddStatus1byteFrame.cmd == NODE_ADD_STATUS)) {
      /* Save space for DSK, dsk length, supported keys and reserved
       * fields in node add status command */
      max = ((sizeof(nms.raw_buf)
              - offsetof(ZW_NODE_ADD_STATUS_1BYTE_FRAME, commandClass1))
             - (3 + sizeof(nms.just_included_dsk)));
    } else {
      /* save space  for zero dsk length, supported keys and reserved
       * fields iin node add status command */
      max = (sizeof(nms.raw_buf)
             - offsetof(ZW_NODE_ADD_STATUS_1BYTE_FRAME, commandClass1))
            - 3;
    }

    /* First 2 bytes of endpoing_info are genericDeviceClass and
     * specificDeviceClass */
    if ((ep->endpoint_info_len - 2) > max) {
      LOG_PRINTF("node info length is more than size of nms buffer. "
                 "Truncating\n");
      assert(0);
    }
    /* Add all occurrences of COMMAND_CLASS_ASSOCIATION
     * with _IP_ASSOCIATION */
    len = nm_buf_insert_data(&r->commandClass1,
                             &ep->endpoint_info[2],
                             COMMAND_CLASS_ASSOCIATION,
                             COMMAND_CLASS_IP_ASSOCIATION,
                             ep->endpoint_info_len - 2,
                             max);
    if (len > max) {
      assert(0);
    }
    /* The magic number 6 below because nodeInfoLength also covers itself
          and following fields BYTE      nodeInfoLength; BYTE properties1;
          BYTE      properties2;
          BYTE      basicDeviceClass;
          BYTE      genericDeviceClass;
          BYTE      specificDeviceClass;          */
    r->nodeInfoLength = len + 6;

    nms.buf_len = r->nodeInfoLength; // 6 for fields below nodeInfoLength
                                     // and above commandClass1 in
                                     // ZW_NODE_ADD_STATUS_1BYTE_FRAME
  } else {
    /* Node probing failed, we know nothing about the supported CC so it
     * should be an empty set. Here we set the length to 6 for following
     * fields only.
     * BYTE      nodeInfoLength;
     * BYTE      properties1;
     * BYTE      properties2;
     * BYTE      basicDeviceClass;
     * BYTE      genericDeviceClass;
     * BYTE      specificDeviceClass;
     */
    r->nodeInfoLength = 6;
    nms.buf_len       = r->nodeInfoLength;
  }
  nms.buf_len += 6; // 6 for fields above nodeInfoLength in
                    // ZW_NODE_ADD_STATUS_1BYTE_FRAME
}

static void nm_net_wait_probe_after_add_handler(nm_event_t ev, void *event_data)
{
  if (ev != NM_EV_NODE_PROBE_DONE) {
    return;
  }

  rd_node_database_entry_t *ndbe = (rd_node_database_entry_t *) event_data;
  if (ndbe->nodeid != nms.tmp_node) {
    return;
  }

  if (nms.cmd == NODE_ADD) {
    sl_nm_net_wait_probe_after_add_node(ndbe);
  } else if ((nms.cmd == FAILED_NODE_REPLACE)
             && (((uint8_t *) &nms.buf)[0]
                 == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION)
             && (((uint8_t *) &nms.buf)[1] == FAILED_NODE_REPLACE_STATUS)) {
    nms.inclusion_flags = 0;
    // LOG_PRINTF("Setting the granted keys :%x \n", reply->grantedKeys);
    goto skip; // stuff below is for add node status command
  }

  nms.buf_len += 2;
  nms.inclusion_flags = 0;

  /* Extended Node Add Status has no DSK fields */
  if (nms.buf.ZW_NodeAddStatus1byteFrame.cmd == NODE_ADD_STATUS) {
    sli_nm_net_wait_probe_add_status();
  }
  skip:
  nms.state = NM_WAIT_DHCP;

  sl_sleeptimer_start_timer_ms(&nms.timer,
                               5000,
                               sl_nm_timer_timeout_cb,
                               NULL,
                               1,
                               0);
}

static void nm_net_set_default_handler(nm_event_t ev, void *event_data)
{
  (void) ev;
  (void) event_data;
  LOG_PRINTF("Do not support in this version\n");
}

static void nm_net_wait_mdns_handler(nm_event_t ev, void *event_data)
{
  (void) ev;
  (void) event_data;
  LOG_PRINTF("Do not support in this version\n");
}

static void nm_net_wait_probe_sis_handler(nm_event_t ev, void *event_data)
{
  (void) ev;
  (void) event_data;
  LOG_PRINTF("Do not support in this version\n");
}

static void nm_net_wait_secure_learn_handler(nm_event_t ev, void *event_data)
{
  (void) ev;
  (void) event_data;
  LOG_PRINTF("Do not support in this version\n");
}

static void nm_net_wait_dhcp_handler(nm_event_t ev, void *event_data)
{
  if ((ev == NM_EV_DHCP_DONE && nms.tmp_node == *(uint16_t *) event_data)
      || (ev == NM_EV_TIMEOUT)) {
    LOG_PRINTF("Do not support in this version\n");
    return;
  }
}

static void nm_net_wait_suc_inclusion_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  if (ev == NM_EV_PROXY_COMPLETE) {
    uint32_t flags =
      NODE_FLAG_SECURITY0 | NODE_FLAG_SECURITY2_UNAUTHENTICATED
      | NODE_FLAG_SECURITY2_AUTHENTICATED
      | NODE_FLAG_SECURITY2_ACCESS; /*TODO this is not a proper view*/

    /* GW can only probe those security keys it knows */
    flags &= sl_get_cache_entry_flag(MyNodeID);
    nms.state = NM_WAIT_FOR_SECURE_ADD;
    nm_net_post_event(NM_EV_SECURITY_DONE, &flags);
  }
}

static void nm_net_waiting_add_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  if (ev == NM_NODE_ADD_STOP || ev == NM_EV_TIMEOUT) {
    nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
    ZW_AddNodeToNetwork(ADD_NODE_STOP, nm_add_node_status_update);
    nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
    nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;
    nm_net_send_reply_to_zip(ev, event_data);
    return;
  } else if (ev == NM_EV_ADD_NODE_FOUND) {
    nms.state = NM_NODE_FOUND;
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 60,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);
  }
}

static void nm_net_prepare_suc_inclusion_handler(nm_event_t ev,
                                                 void *event_data)
{
  (void) event_data;
  if (ev == NM_EV_TIMEOUT) {
    nms.state = NM_WIAT_FOR_SUC_INCLUSION;
    request_inclusion_controller_handover(nms.tmp_node,
                                          (nms.cmd == FAILED_NODE_REPLACE),
                                          &inclusion_controller_complete);
  }
}

static void nm_net_neighbor_update_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  if ((ev == NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE)
      || (ev == NM_EV_TIMEOUT)) {
    LOG_PRINTF("Delayed node Neighbor Discovery done or timed out after S2 "
               "inclusion.\n");
    nms.state = NM_WAIT_FOR_PROBE_AFTER_ADD;
    rd_probe_lock(FALSE);
  }
}

static void nm_net_our_probe_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  if (ev == NM_EV_ALL_PROBED) {
    if ((nms.flags & NMS_FLAG_LEARNMODE_NEW)) {
      /* Wait until probing is done to send LEARN_MODE_INTERVIEW_COMPLETED */
      LOG_PRINTF("Sending LEARN_MODE_INTERVIEW_COMPLETED\n");
      nm_send_reply_net_updated();
    }
  }
}

static void nm_net_proxy_nif_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_TIMEOUT) {
    nms.state = NM_IDLE;
  } else if (ev == NM_EV_NODE_INFO) {
    NODEINFO ni;

    if (nms.cmd == NODE_ADD) {
      memset(&nms.buf, 0, sizeof(nms.buf));
      nms.buf.ZW_NodeAddStatus1byteFrame.cmdClass =
        COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      nms.buf.ZW_NodeAddStatus1byteFrame.cmd            = NODE_ADD_STATUS;
      nms.buf.ZW_NodeAddStatus1byteFrame.seqNo          = nms.seq;
      nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength = 1;
      nms.buf_len = nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength + 6;

      /* Get the Capabilities and Security fields. */
      sl_zw_get_node_proto_info(nms.tmp_node, &ni);
      nms.buf.ZW_NodeAddStatus1byteFrame.properties1 = ni.capability;
      nms.buf.ZW_NodeAddStatus1byteFrame.properties2 = ni.security;

      nms.flags = NMS_FLAG_S2_ADD | NMS_FLAG_PROXY_INCLUSION;

      /* Simulate the add process */
      nms.state = NM_NODE_FOUND;
      nm_net_post_event(NM_EV_ADD_CONTROLLER, event_data);

      nm_net_post_event(NM_EV_ADD_NODE_STATUS_DONE, event_data);
    } else {
      ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *reply =
        (ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX *) &nms.buf;
      nms.cmd            = FAILED_NODE_REPLACE;
      nms.state          = NM_REPLACE_FAILED_REQ;
      reply->cmdClass    = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      reply->cmd         = FAILED_NODE_REPLACE_STATUS;
      reply->seqNo       = nms.seq;
      reply->nodeId      = nms.tmp_node;
      reply->status      = ZW_FAILED_NODE_REPLACE_FAILED;
      reply->kexFailType = 0x00;
      reply->grantedKeys = 0x00;
      nms.buf_len        = sizeof(ZW_FAILED_NODE_REPLACE_STATUS_FRAME_EX);

      nms.flags = NMS_FLAG_S2_ADD | NMS_FLAG_PROXY_INCLUSION;
      nm_net_post_event(NM_EV_REPLACE_FAILED_DONE, event_data);
    }
  }
}

static void nm_net_learn_mode_handler(nm_event_t ev, void *event_data)
{
  if (ev == NM_EV_TIMEOUT) {
    if (nms.count == 0) {
      /* We must stop Learn Mode classic before starting NWI learn mode */
      ZW_SetLearnMode(ZW_SET_LEARN_MODE_DISABLE, NULL);
      if (nms.flags & NMS_FLAG_LEARNMODE_NWI) {
        ZW_SetLearnMode(ZW_SET_LEARN_MODE_NWI, nm_learn_mode_status_update);
      } else if (nms.flags & NMS_FLAG_LEARNMODE_NWE) {
        ZW_SetLearnMode(ZW_SET_LEARN_MODE_NWE, nm_learn_mode_status_update);
      }
    }

    if ((nms.flags & (NMS_FLAG_LEARNMODE_NWI | NMS_FLAG_LEARNMODE_NWE))
        && (nms.count < 4)) {
      if (nms.flags & NMS_FLAG_LEARNMODE_NWI) {
        ZW_ExploreRequestInclusion();
      } else {
        ZW_ExploreRequestExclusion();
      }

      int delay = CLOCK_SECOND * 4 + (rand() & 0xFF);
      sl_sleeptimer_start_timer_ms(&nms.timer,
                                   delay,
                                   sl_nm_timer_timeout_cb,
                                   NULL,
                                   1,
                                   0);
      nms.count++;
    } else {
      nm_learn_timer_expired();
    }
  } else if (ev == NM_EV_LEARN_SET) {
    ZW_LEARN_MODE_SET_FRAME *f = (ZW_LEARN_MODE_SET_FRAME *) event_data;

    if (f->mode == ZW_SET_LEARN_MODE_DISABLE) {
      nms.seq = f->seqNo; // Just because this was how we did in 2.2x
      nm_learn_timer_expired();
    }
  }
}

static void nm_net_self_destruct_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  if (ev == NM_EV_TIMEOUT) {
    if (nms.state == NM_WAIT_FOR_SELF_DESTRUCT) {
      nms.state = NM_WAIT_FOR_TX_TO_SELF_DESTRUCT;
    } else {
      nms.state = NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY;
    }
  }
}

static void nm_net_tx_self_destruct_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  /* As a preparation for removing the self-destructed node,
   * we must attempt TX to it. Otherwise the protocol will
   * not allow us to do ZW_RemoveFailed() on it. */
  if (ev == NM_EV_TX_DONE_SELF_DESTRUCT) {
    /* Perform remove failed */
    if (nms.state == NM_WAIT_FOR_TX_TO_SELF_DESTRUCT) {
      nms.state = NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL;
      LOG_PRINTF("Removing self destruct nodeid %u\n", nms.tmp_node);
    } else {
      nms.state = NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY;
      LOG_PRINTF("Retry of removing self destruct nodeid %u\n", nms.tmp_node);
    }
    nms.buf_len += nms.buf.ZW_NodeAddStatus1byteFrame.nodeInfoLength - 1;
    assert(nms.tmp_node);
    sl_sleeptimer_start_timer_ms(&nms.timer,
                                 CLOCK_SECOND * 20,
                                 sl_nm_timer_timeout_cb,
                                 NULL,
                                 1,
                                 0);

    if (ZW_RemoveFailedNode(nms.tmp_node, nm_remove_self_destruct_status)
        != ZW_FAILED_NODE_REMOVE_STARTED) {
      LOG_PRINTF("Remove self-destruct failed\n");
      nm_remove_self_destruct_status(ZW_FAILED_NODE_NOT_REMOVED);
    }
  }
}

static void nm_net_destruct_remove_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  /* Send the NODE_ADD_STATUS zip packet with proper status code to both unsoc
   * dest and reset the NM state. We return ADD_NODE_STATUS_FAILED on
   * ZW_FAILED_NODE_REMOVE and ADD_NODE_SECURITY_FAILED otherwise. This is
   * required by spec.
   */
  if (ev == NM_EV_REMOVE_FAILED_OK) {
    nms.buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
    /* Delete the removed node-id from the RD and DHCP. */
    /* In a corner case, the device may believe it was included and
     * security succeeded and then not self-destruct (eg, it is out
     * of reach).  In that case, the device must be explicitly reset
     * by a user. */
    sl_appl_controller_update(UPDATE_STATE_DELETE_DONE,
                              nms.tmp_node,
                              0,
                              0,
                              NULL);
    /* Unlock probe engine, but there is no node to probe, so just
     * cancel it. */
  } else if (ev == NM_EV_REMOVE_FAILED_FAIL) {
    // this is first attempt removed failed fail. Try again in 240s.
    if (nms.state == NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL) {
      nms.state = NM_WAIT_FOR_SELF_DESTRUCT_RETRY;
      sl_sleeptimer_start_timer(&nms.timer,
                                SMART_START_SELF_DESTRUCT_RETRY_TIMEOUT,
                                sl_nm_timer_timeout_cb,
                                0,
                                1,
                                0);
    } else if (nms.state == NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY) {
      nms.buf.ZW_NodeAddStatus1byteFrame.status =
        ADD_NODE_STATUS_SECURITY_FAILED;
      /* Unlock the probe engine. */
      rd_probe_cancel();
      // DS
#if UNSOC_SUPPORTED
      /* TODO: we may want to probe the new node before we re-start
       * smart start. */
      send_to_both_unsoc_dest((uint8_t *) &nms.buf,
                              nms.buf_len,
                              nm_net_reset_state);
#endif //UNSOC_SUPPORTED
    }
  } else if (ev == NM_EV_TIMEOUT) {
    /* Protocol should always call back, but if it did not, we reset as a
     * fall-back*/
    LOG_PRINTF(
      "Timed out waiting for ZW_RemoveFailed() of self-destruct node\n");
    /* Unlock the probe engine */
    rd_probe_cancel();
    nm_net_reset_state(0, 0, 0);
  }
}

static void nm_net_node_info_handler(nm_event_t ev, void *event_data)
{
  (void) event_data;
  if (ev == NM_EV_NODE_PROBE_DONE) {
    rd_node_database_entry_t *ndbe = (rd_node_database_entry_t *) event_data;
    if (ndbe->nodeid == nms.tmp_node) {
      ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *f =
        (ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *) &nms.buf;
      int len = nm_build_node_cached_report(ndbe, f);
      nm_send_reply(f, 10 + len);
    }
    /* If this is not the node probe NM is looking for, do nothing. */
  }
}

static void nm_net_fsm_event_handler(nm_event_t ev, void *event_data)
{
  LOG_PRINTF("nm_net_post_event event: %s state: %s\n",
             nm_event_name(ev),
             nm_state_name(nms.state));

  // clear scheme S0.
  nm_net_scheme_clear(ev);

  switch (nms.state) {
    case NM_IDLE:
      nm_net_idle_state_handler(ev, event_data);
      break;
    case NM_REPLACE_FAILED_REQ:
      nm_net_replace_failed_req_handler(ev, event_data);
      break;
    case NM_WAITING_FOR_ADD:
      nm_net_waiting_add_handler(ev, event_data);
      break;
    case NM_NODE_FOUND:
      nm_net_node_found_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_PROTOCOL:
      nm_net_wait_protocol_handler(ev, event_data);
      break;
    case NM_PREPARE_SUC_INCLISION:
      nm_net_prepare_suc_inclusion_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD:
      nm_net_neighbor_update_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_SECURE_ADD:
      nm_net_wait_secure_add_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_PROBE_AFTER_ADD:
      nm_net_wait_probe_after_add_handler(ev, event_data);
      break;
    case NM_WAIT_DHCP:
      nm_net_wait_dhcp_handler(ev, event_data);
      break;
    case NM_SET_DEFAULT:
      nm_net_set_default_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_MDNS:
      nm_net_wait_mdns_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_PROBE_BY_SIS:
      nm_net_wait_probe_sis_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_OUR_PROBE:
      nm_net_our_probe_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_SECURE_LEARN:
      nm_net_wait_secure_learn_handler(ev, event_data);
      break;
    case NM_WIAT_FOR_SUC_INCLUSION:
      nm_net_wait_suc_inclusion_handler(ev, event_data);
      break;
    case NM_PROXY_INCLUSION_WAIT_NIF:
      nm_net_proxy_nif_handler(ev, event_data);
      break;
    case NM_LEARN_MODE:
      nm_net_learn_mode_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_SELF_DESTRUCT:
    case NM_WAIT_FOR_SELF_DESTRUCT_RETRY:
      nm_net_self_destruct_handler(ev, event_data);
      break;
    case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT:
    case NM_WAIT_FOR_TX_TO_SELF_DESTRUCT_RETRY:
      nm_net_tx_self_destruct_handler(ev, event_data);
      break;

    case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL:
    case NM_WAIT_FOR_SELF_DESTRUCT_REMOVAL_RETRY:
      nm_net_destruct_remove_handler(ev, event_data);
      break;

    case NM_WAIT_FOR_NODE_INFO_PROBE:
      nm_net_node_info_handler(ev, event_data);
      break;

    case NM_WAITING_FOR_NODE_NEIGH_UPDATE:
      if (ev == NM_EV_TIMEOUT) {
        /* When there are FLIRS devices in the network the protocol will not
         * activate the status callback (see SWPROT-3666). We do that here to
         * avoid blocking network management forever.
         */
        nm_request_node_neighbor_status(REQUEST_NEIGHBOR_UPDATE_FAILED);
      }
      break;

    case NM_LEARN_MODE_STARTED:
    case NM_NETWORK_UPDATE:
    case NM_WAITING_FOR_PROBE:
    case NM_REMOVING_ASSOCIATIONS:
    case NM_SENDING_NODE_INFO:
    case NM_WAITING_FOR_NODE_REMOVAL:
    case NM_WAITING_FOR_FAIL_NODE_REMOVAL:
    case NM_WAITING_FOR_RETURN_ROUTE_ASSIGN:
    case NM_WAITING_FOR_RETURN_ROUTE_DELETE:

      break;
  }
}

static void nm_net_post_event(nm_event_t ev, void *event_data)
{
  sl_cc_net_ev_t msg = { .ev = ev, .ev_data = event_data };

  osMessageQueuePut(sli_manager_queue, (void *) &msg, 0, osWaitForever);
}

void NetworkManagement_nif_notify(nodeid_t bNodeID, uint8_t *pCmd, uint8_t bLen)
{
  LEARN_INFO info;
  info.bStatus = ADD_NODE_STATUS_ADDING_END_NODE;
  info.bSource = bNodeID;
  info.pCmd    = pCmd;
  info.bLen    = bLen;

  nm_net_post_event(NM_EV_NODE_INFO, &info);
}

void sl_nm_start_proxy_inclusion(nodeid_t node_id)
{
  nm_net_post_event(NM_EV_START_PROXY_INCLUSION, &node_id);
}

void sl_nm_start_proxy_replace(nodeid_t node_id)
{
  nm_net_post_event(NM_EV_START_PROXY_REPLACE, &node_id);
}

void NetworkManagement_all_nodes_probed()
{
  nm_net_post_event(NM_EV_ALL_PROBED, 0);
}

/* Post NM_EV_NODE_PROBE_DONE */
void NetworkManagement_node_probed(void *node)
{
  nm_net_post_event(NM_EV_NODE_PROBE_DONE, node);
}

static void nm_secure_inclusion_done(int status)
{
  uint32_t safe_status = status;
  nm_net_post_event(NM_EV_SECURITY_DONE, &safe_status);
}

static void nm_timer_timeout_cb(void *u)
{
  (void) u;
  nm_net_post_event(NM_EV_TIMEOUT, 0);
}

void sl_nm_virtual_nodes_removed()
{
  if (nms.state == NM_REMOVING_ASSOCIATIONS) {
    nm_send_reply(&nms.buf, nms.buf_len);
  }
}

void sl_nm_net_update_status(u8_t flag)
{
  networkUpdateStatusFlags |= flag;

  if (bridge_state != booting) {
    networkUpdateStatusFlags |= NETWORK_UPDATE_FLAG_VIRTUAL;
  }

  LOG_PRINTF("update flag 0x%x 0x%x\n", flag, networkUpdateStatusFlags);
  if (networkUpdateStatusFlags
      == (NETWORK_UPDATE_FLAG_DHCPv4 | NETWORK_UPDATE_FLAG_PROBE
          | NETWORK_UPDATE_FLAG_VIRTUAL)) {
    nm_send_reply(&nms.buf, nms.buf_len);
  }
}

static void network_update_timeout()
{
  sl_nm_net_update_status(NETWORK_UPDATE_FLAG_DHCPv4
                          | NETWORK_UPDATE_FLAG_PROBE);
}

/**
 * Setup the transmission of nms.buf when the Network update, node probing and
 * Ipv4 assignment of new nodes has completed.
 */
static void nm_send_reply_net_updated()
{
  nms.state                = NM_WAITING_FOR_PROBE;
  networkUpdateStatusFlags = 0;

  if ((bridge_state == initialized) || (controller_role != CTRL_SUC)) {
    networkUpdateStatusFlags |= NETWORK_UPDATE_FLAG_VIRTUAL;
  }

  /* Wait 65 secs */

  rd_probe_lock(FALSE);

  /*Check the we actually allocated the timer */
  if (nms.networkManagementTimer == 0xFF) {
    network_update_timeout();
  }
}

/**
 * Reset the network management state to #NM_IDLE.
 *
 * Cancel the NM timer and clear all the flags in #nms.  Set
 * #NETWORK_UPDATE_FLAG_DISABLED on #networkUpdateStatusFlags.
 *
 * Try to restart Smart Start.  Post #ZIP_EVENT_NETWORK_MANAGEMENT_DONE to \ref
 * ZIP_Router.
 */
static void nm_net_reset_state(BYTE dummy, void *user, TX_STATUS_TYPE *t)
{
  (void) t;
  (void) dummy;
  (void) user;
  LOG_PRINTF("Reset Network management State\n");
  nms.networkManagementTimer = 0xFF;
  nms.cmd                    = 0;
  nms.waiting_for_ipv4_addr  = 0;
  nms.buf_len                = 0;
  nms.flags                  = 0;
  networkUpdateStatusFlags   = 0x80;
  nms.state                  = NM_IDLE;
  nms.granted_keys           = 0;
}

static void __ResetState(BYTE dummy)
{
  nm_net_reset_state(dummy, 0, NULL);
}

/**
 * Timeout of learn mode.
 *
 * Tell the protocol to stop learn mode.  Restore gateway state by
 * calling the #nm_learn_mode_status_update() callback, simulating learn mode fail.
 *
 * Called from \ref nm_net_post_event() when the nm timer expires in
 * #NM_LEARN_MODE or if the client cancels learn mode.
 *
 * In cancel, the gateway receives a #LEARN_MODE_SET command with mode
 * \a ZW_SET_LEARN_MODE_DISABLE.
 *
 * \note The gateway is always in #NM_LEARN_MODE when this function is
 * called, but since this is asynchronous, there is a tiny probability
 * that a callback with #LEARN_MODE_STARTED is in the queue for us.
 * In that case, the protocol is already committed, and it would be
 * wrong to cancel learn mode.
 *
 */
static void nm_learn_timer_expired()
{
  LEARN_INFO inf;
  LOG_PRINTF("Learn timed out or canceled\n");
  /*Restore command classes as they were */
  ZW_SetLearnMode(ZW_SET_LEARN_MODE_DISABLE, 0);
  inf.bStatus = LEARN_MODE_FAILED;
  nm_learn_mode_status_update(&inf);
}

void sl_nm_learn_timer_expired(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) t;
  (void) u;
  nm_learn_timer_expired();
}

/**
 * Timeout for remove node.
 */
static void RemoveTimerExpired(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) t;
  (void) u;

  LEARN_INFO inf;

  // LOG_PRINTF("Remove timed out or canceled\n");
  ZW_RemoveNodeFromNetwork(REMOVE_NODE_STOP, 0);
  inf.bStatus = REMOVE_NODE_STATUS_FAILED;
  inf.bSource = 0;
  nm_remove_node_status_update(&inf);
}

/**
 * Generic wrapper to send a reply to the host whom we are talking to.
 */
static void nm_send_reply(void *buf, u16_t len)
{
  (void) len;
  unsigned char *c = (uint8_t *) buf;
  LOG_PRINTF("Sending network management reply: class: 0x%02x cmd: 0x%02x\n",
             c[0],
             c[1]);
}

/**
 * Callback for ZW_AddNodeToNetwork
 */
static void nm_add_node_status_update(LEARN_INFO *inf)
{
  nm_net_post_event(inf->bStatus, inf);
}

/**
 * Remove a self-destructed node callback
 * Notify the FSM of status of RemoveFailed for self-destructed node.
 *
 * \param status One of the ZW_RemoveFailedNode() callback statuses:
 *        ZW_NODE_OK
 *        ZW_FAILED_NODE_REMOVED
 *        ZW_FAILED_NODE_NOT_REMOVED.
 *
 * ZW_NODE_OK must be considered a _FAIL because that will send _SECURITY_FAILED
 * to unsolicited destination, which is what the spec requires in case a
 * failed smart start node could not be removed from the network.
 */
static void nm_remove_self_destruct_status(BYTE status)
{
  if (status == ZW_FAILED_NODE_REMOVED) {
    nm_net_post_event(NM_EV_REMOVE_FAILED_OK, 0);
  } else {
    nm_net_post_event(NM_EV_REMOVE_FAILED_FAIL, 0);
  }
}

/*
 * Replace failed node callback
 */
static void nm_replace_failed_node_status_update(BYTE status)
{
  switch (status) {
    case ZW_FAILED_NODE_REPLACE:
      LOG_PRINTF("Ready to replace node....\n");
      break;
    case ZW_FAILED_NODE_REPLACE_DONE:
      nm_net_post_event(NM_EV_REPLACE_FAILED_DONE, 0);
      break;
    case ZW_NODE_OK:

    /* no break */
    case ZW_FAILED_NODE_REPLACE_FAILED:
      nm_net_post_event(NM_EV_REPLACE_FAILED_FAIL, 0);
      break;
  }
}

/**
 * Callback for remove node
 */
static void nm_remove_node_status_update(LEARN_INFO *inf)
{
  static nodeid_t removed_nodeid;
  ZW_NODE_REMOVE_STATUS_V4_FRAME *r =
    (ZW_NODE_REMOVE_STATUS_V4_FRAME *) &nms.buf;
  LOG_PRINTF("nm_remove_node_status_update status=%d node %d\n",
             inf->bStatus,
             inf->bSource);
  switch (inf->bStatus) {
    case ADD_NODE_STATUS_LEARN_READY:
      memset(&nms.buf, 0, sizeof(nms.buf));
      /* Start remove timer */
      sl_sleeptimer_start_timer_ms(&nms.long_timer,
                                   ADD_REMOVE_TIMEOUT,
                                   RemoveTimerExpired,
                                   NULL,
                                   1,
                                   0);
      break;
    case REMOVE_NODE_STATUS_NODE_FOUND:
      break;
    case REMOVE_NODE_STATUS_REMOVING_END_NODE:
    case REMOVE_NODE_STATUS_REMOVING_CONTROLLER:
      if (is_lr_node(inf->bSource)) {
        r->nodeid            = 0xff;
        r->extendedNodeidMSB = inf->bSource >> 8;
        r->extendedNodeidLSB = inf->bSource & 0xff;
      } else {
        r->extendedNodeidMSB = 0;
        r->extendedNodeidLSB = 0;
        r->nodeid            = inf->bSource;
      }
      removed_nodeid = inf->bSource;
      break;
    case REMOVE_NODE_STATUS_DONE:
      LOG_PRINTF("Node Removed %d\n", removed_nodeid);
      nms.state = NM_REMOVING_ASSOCIATIONS;
      /* Application controller update will call ip_assoc_remove_by_nodeid()
       * which will post a ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE, which will then
       * call sl_nm_virtual_nodes_removed */

      sl_appl_controller_update(UPDATE_STATE_DELETE_DONE,
                                removed_nodeid,
                                0,
                                0,
                                NULL);
    /*fall through */
    case REMOVE_NODE_STATUS_FAILED:
      r->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
      r->cmd      = NODE_REMOVE_STATUS;
      r->status   = inf->bStatus;
      r->seqNo    = nms.seq;

      nms.buf_len = sizeof(ZW_NODE_REMOVE_STATUS_V4_FRAME);
      if ((inf->bStatus == REMOVE_NODE_STATUS_FAILED)
          || nodemask_nodeid_is_invalid(removed_nodeid)) {
        r->nodeid            = 0;
        r->extendedNodeidMSB = 0;
        r->extendedNodeidLSB = 0;
      }

      sl_sleeptimer_stop_timer(&nms.long_timer);
      ZW_RemoveNodeFromNetwork(REMOVE_NODE_STOP, 0);
      nm_send_reply(r, sizeof(ZW_NODE_REMOVE_STATUS_V4_FRAME));
      break;
  }
}

/**
 * Remove failed node callback
 */
static void RemoveFailedNodeStatus(BYTE status)
{
  ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME *f =
    (ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME *) &nms.buf;
  BYTE s = NM_FAILED_NODE_REMOVE_FAIL;

  LOG_PRINTF("RemoveFailedNodeStatus status: %d\n", status);

  /* Mapping ZW_RemoveFailedNode return values to requirement
   * CC.0034.01.08.11.001 */
  if (status == ZW_NODE_OK || status == ZW_FAILED_NODE_NOT_REMOVED) {
    s = NM_FAILED_NODE_REMOVE_FAIL;
  } else if (status == ZW_FAILED_NODE_NOT_FOUND) {
    s = NM_FAILED_NODE_NOT_FOUND;
  } else if (status == ZW_FAILED_NODE_REMOVED) {
    s = NM_FAILED_NODE_REMOVE_DONE;
  }

  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  f->cmd      = FAILED_NODE_REMOVE_STATUS;
  f->seqNo    = nms.seq;
  f->status   = s;
  if (is_lr_node(nms.tmp_node)) {
    f->nodeId            = 0xff;
    f->extendedNodeIdMSB = nms.tmp_node >> 8;
    f->extendedNodeIdLSB = nms.tmp_node & 0xff;
  } else {
    f->nodeId            = nms.tmp_node;
    f->extendedNodeIdMSB = 0;
    f->extendedNodeIdLSB = 0;
  }
  nm_send_reply(f, sizeof(ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME));

  if (status == ZW_FAILED_NODE_REMOVED) {
    LOG_PRINTF("Failed node Removed%d\n", nms.tmp_node);
  }
}

/**
 * Set default callback.
 *
 * Used in the \a ZW_SetDefault() call when NMS is processing \a
 * DEFAULT_SET.  NMS should be in state #NM_SET_DEFAULT.
 *
 * Prepare NMS reply buffer and call rd_exit() to tear down mDNS and RD.
 */
static void SetDefaultStatus()
{
  nms.buf.ZW_DefaultSetCompleteFrame.cmdClass =
    COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
  nms.buf.ZW_DefaultSetCompleteFrame.cmd    = DEFAULT_SET_COMPLETE;
  nms.buf.ZW_DefaultSetCompleteFrame.seqNo  = nms.seq;
  nms.buf.ZW_DefaultSetCompleteFrame.status = DEFAULT_SET_DONE;
  nms.buf_len = sizeof(nms.buf.ZW_DefaultSetCompleteFrame);
  LOG_PRINTF("Controller reset done\n");
}

static void RequestNodeNeighborUpdat_callback_after_s2_inclusion(BYTE status)
{
  if (nms.state == NM_WAIT_FOR_NEIGHBOR_UPDATE_AFTER_SECURE_ADD) {
    switch (status) {
      case REQUEST_NEIGHBOR_UPDATE_STARTED:
        break;
      case REQUEST_NEIGHBOR_UPDATE_DONE:
      case REQUEST_NEIGHBOR_UPDATE_FAILED:
        nm_net_post_event(NM_EV_NEIGHBOR_UPDATE_AFTER_SECURE_ADD_DONE, 0);
        break;
    }
  }
}
/*
 * ZW_RequestNeighborUpdate callback
 */
static void nm_request_node_neighbor_status(BYTE status)
{
  /* We need to ensure we're still in the correct state.
   *
   * As a workaround to SWPROT-3666 the gateway has wrapped the call to
   * ZW_RequestNodeNeighborUpdate() in a timer. If/when the protocol is fixed we
   * can risk that the callback comes from the protocol after the gateway has
   * timed out and moved on to something else.
   */
  if (nms.state == NM_WAITING_FOR_NODE_NEIGH_UPDATE) {
    switch (status) {
      case REQUEST_NEIGHBOR_UPDATE_STARTED:
        break;
      case REQUEST_NEIGHBOR_UPDATE_DONE:
      case REQUEST_NEIGHBOR_UPDATE_FAILED:
        nms.buf.ZW_NodeNeighborUpdateStatusFrame.cmdClass =
          COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
        nms.buf.ZW_NodeNeighborUpdateStatusFrame.cmd =
          NODE_NEIGHBOR_UPDATE_STATUS;
        nms.buf.ZW_NodeNeighborUpdateStatusFrame.seqNo  = nms.seq;
        nms.buf.ZW_NodeNeighborUpdateStatusFrame.status = status;
        nm_send_reply(&nms.buf,
                      sizeof(nms.buf.ZW_NodeNeighborUpdateStatusFrame));
        break;
    }
  }
}

void NetworkManagement_mdns_exited()
{
  nm_net_post_event(NM_EV_MDNS_EXIT, 0);
}

void NetworkManagement_s0_started()
{
  nm_net_post_event(NM_EV_S0_STARTED, 0);
}

void NetworkManagement_frame_notify()
{
  nm_net_post_event(NM_EV_FRAME_RECEIVED, 0);
}

const char *learn_mode_status_str(int ev)
{
  static char str[25];
  switch (ev) {
    case LEARN_MODE_STARTED:
      return "LEARN_MODE_STARTED";
    case LEARN_MODE_DONE:
      return "LEARN_MODE_DONE";
    case LEARN_MODE_FAILED:
      return "LEARN_MODE_FAILED";
    case LEARN_MODE_INTERVIEW_COMPLETED:
      return "LEARN_MODE_INTERVIEW_COMPLETED";
    default:
      sprintf(str, "%d", ev);
      return str;
  }
}

#if 0
static void
reset_delayed()
{
  process_post(&zip_process, ZIP_EVENT_RESET, 0);
}
#endif

/**
 * Return true is this is a clean network containing only the ZIP router and
 * if this is a new network compared to what we have en RAM
 */
static void isCleanNetwork(BOOL *clean_network, BOOL *new_network)
{
  BYTE ver, capabilities, len;
  BYTE node_list[MAX_CLASSIC_NODEMASK_LENGTH] = { 0 };
  BYTE c, v;
  DWORD h;
  nodeid_t n;
  int i;
  uint16_t lr_nodelist_len                    = 0;
  uint8_t lr_nodelist[MAX_LR_NODEMASK_LENGTH] = { 0 };

  MemoryGetID((BYTE *) &h, &n);
  SerialAPI_GetInitData(&ver, &capabilities, &len, node_list, &c, &v);

  *new_network = (h != homeID);

  node_list[(n - 1) >> 3] &= ~(1 << ((n - 1) & 0x7));
  for (i = 0; i < MAX_CLASSIC_NODEMASK_LENGTH; i++) {
    if (node_list[i]) {
      *clean_network = FALSE;
      return;
    }
  }
  SerialAPI_GetLRNodeList(&lr_nodelist_len, (uint8_t *) &lr_nodelist);
  for (i = 0; i < lr_nodelist_len; i++) {
    if (lr_nodelist[i]) {
      *clean_network = FALSE;
      return;
    }
  }

  *clean_network = TRUE;
}

static void nm_learn_mode_status_update(LEARN_INFO *inf)
{
  BOOL clean_network, new_network;
  //  static nodeid_t old_nodeid;

  if ((nms.state != NM_LEARN_MODE) && (nms.state != NM_LEARN_MODE_STARTED)) {
    LOG_PRINTF(
      "nm_learn_mode_status_update callback while not in learn mode\n");
    return;
  }

  ZW_LEARN_MODE_SET_STATUS_FRAME *f =
    (ZW_LEARN_MODE_SET_STATUS_FRAME *) &nms.buf;
  LOG_PRINTF("learn mode %s\n", learn_mode_status_str(inf->bStatus));

  switch (inf->bStatus) {
    case LEARN_MODE_STARTED:
      // rd_probe_lock(TRUE);
      /* Set my nodeID to an invalid value, to keep controller updates from
       * messing things up*/
      MyNodeID     = 0;
      nms.tmp_node = inf->bSource;
      nms.state    = NM_LEARN_MODE_STARTED;

      break;
    case LEARN_MODE_DONE:

      /*There are three outcomes of learn mode
       * 1) Controller has been included into a new network
       * 2) Controller has been excluded from a network
       * 3) Controller replication or controller shift
       * */
      isCleanNetwork(&clean_network, &new_network);

      nms.state = NM_WAIT_FOR_SECURE_LEARN;

      if (clean_network || inf->bSource == 0) {
        LOG_PRINTF("Z/IP Gateway has been excluded.\n");
        MyNodeID = 1;
        /* Enable DHCP to hang on to the IP addr of the gateway. */

        //      ipv46nat_rename_node(old_nodeid, MyNodeID);
        /* Simulate NM_EV_SECURITY_DONE in the NMS.  This will change
         * state to NM_WAIT_FOR_MDNS.  Since this is a clean network, we
         * change that to SET_DEFAULT. */
        nm_secure_inclusion_done(0);
        nms.state = NM_SET_DEFAULT;

        //      process_start(&dhcp_client_process, 0);
      } else if (new_network) {
        /*Update home id and node id, security engine needs to know correct nodeid
         */

        MemoryGetID((BYTE *) &homeID, &MyNodeID);
        MyNodeID = inf->bSource;
      } else {
        nms.flags |= NMS_FLAG_CONTROLLER_REPLICATION; /* or controller shift */

        /*Update home id and node id, security engine needs to know correct nodeid
         */
        MemoryGetID((BYTE *) &homeID, &MyNodeID);

        /*This was a controller replication, ie. this is not a new network. */
        LOG_PRINTF("This was a controller replication\n");
      }

      return;
    case LEARN_MODE_FAILED:
      /* We can only get learn mode FAILED on the serial API during
       * inclusion, and only rarely.  The including controller will
       * assume that we are actually included. */
      /* We also use FAILED internally in nm_learn_timer_expired().  The
       * nm_timer_timeout_cb handler has already called sl_application_nif_init() and
       * disabled learn mode, which is not necessary when getting this
       * callback from serial. */
      rd_probe_lock(FALSE);
      f->cmdClass  = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
      f->cmd       = LEARN_MODE_SET_STATUS;
      f->seqNo     = nms.seq;
      f->status    = inf->bStatus;
      f->newNodeId = 0;
      f->reserved  = 0;
      nm_send_reply(f, sizeof(ZW_LEARN_MODE_SET_STATUS_FRAME));
      break;
  }
}

/* Find out the smallest value (len) allowing to advertise all bits set
 * in the bitmask (used to generate failed node list or node list)
 * For e.g. if there is 1 bit set which is in second byte, then this function
 * will return 2. */
uint16_t find_min_bitmask_len(uint8_t *buffer, uint16_t length)
{
  uint16_t i = 0;
  while ((i < length) && (buffer[i] != 0)) {
    i++;
  }
  return i;
}

static uint16_t nm_build_failed_node_list_frame(uint8_t *buffer, uint8_t seq)
{
  nodeid_t i;
  rd_node_database_entry_t *n;
  nodemask_t nlist = { 0 };
  uint16_t lr_len  = 0;

  ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME *f =
    (ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME *) buffer;
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  f->cmd      = FAILED_NODE_LIST_REPORT;
  f->seqNo    = seq;

  for (i = 1; i <= ZW_MAX_NODES; i++) { // i is node id here
    n = rd_node_get_raw(i);
    if (!n) {
      continue;
    }

    if (i == MyNodeID) {
      continue;
    }
  }
  /* Calculate minimum len of failed node list bitmask that needs to be sent
   * and not whole ZW_MAX_NODES/8 */
  lr_len = find_min_bitmask_len(NODEMASK_GET_LR(nlist), MAX_NODEMASK_LENGTH);

  memcpy(&f->failedNodeListData1, nlist, MAX_CLASSIC_NODEMASK_LENGTH);
  f->extendedNodeidMSB = lr_len >> 8;
  f->extendedNodeidLSB = lr_len & 0xff;
  /* Copy only from ZW_LR_NODEMASK_OFFSET to upto where (len) the last byte
   * has a bit set in extended failed node list*/
  f->extendedNodeListData1 = 0;
  if (lr_len != 0) {
    memcpy(&f->extendedNodeListData1, NODEMASK_GET_LR(nlist), lr_len);
  }
  return (sizeof(ZW_FAILED_NODE_LIST_REPORT_1BYTE_V4_FRAME)
          + (lr_len != 0 ? lr_len - 1 : 0));
}

static uint16_t nm_build_node_list_frame(uint8_t *frame, uint8_t seq)
{
  BYTE ver, capabilities, len, c, v;
  BYTE *nlist;

  ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME *f =
    (ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME *) frame;
  uint16_t lr_nodelist_len                    = 0;
  uint8_t lr_nodelist[MAX_LR_NODEMASK_LENGTH] = { 0 };

  f->cmdClass             = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  f->cmd                  = NODE_LIST_REPORT;
  f->seqNo                = seq;
  f->nodeListControllerId = ZW_GetSUCNodeID();
  nlist                   = &(f->nodeListData1);
  f->status               = (f->nodeListControllerId == MyNodeID)
                            ? NODE_LIST_REPORT_LATEST_LIST
                            : NODE_LIST_REPORT_NO_GUARANTEE;

  SerialAPI_GetInitData(&ver, &capabilities, &len, nlist, &c, &v);

  SerialAPI_GetLRNodeList(&lr_nodelist_len, (uint8_t *) &lr_nodelist);
  /* Calculate minimum len of node list bitmask that needs to be sent and not
   * whole ZW_MAX_NODES/8 */
  lr_nodelist_len =
    find_min_bitmask_len((uint8_t *) &lr_nodelist, MAX_LR_NODEMASK_LENGTH);
  LOG_PRINTF("LR nodelist length: 0x%02X\n", lr_nodelist_len);
  nlist[MAX_CLASSIC_NODEMASK_LENGTH]     = lr_nodelist_len >> 8;
  nlist[MAX_CLASSIC_NODEMASK_LENGTH + 1] = lr_nodelist_len & 0xff;
  memcpy(&nlist[MAX_CLASSIC_NODEMASK_LENGTH + 2],
         &lr_nodelist,
         lr_nodelist_len);

  if (f->nodeListControllerId == 0) {
    c = ZW_GetControllerCapabilities();
    /*This is a non sis network and I'm a primary */
    if ((c & CONTROLLER_NODEID_SERVER_PRESENT) == 0
        && (c & CONTROLLER_IS_SECONDARY) == 0) {
      f->status               = NODE_LIST_REPORT_LATEST_LIST;
      f->nodeListControllerId = MyNodeID;
    }
  }
  // remove 2 from the length for nodeListData1 and extendednodeListData1 from
  // ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME frame
  return (MAX_CLASSIC_NODEMASK_LENGTH
          + sizeof(ZW_NODE_LIST_REPORT_1BYTE_V4_FRAME) - 2 + lr_nodelist_len);
}

static void NetworkUpdateCallback(BYTE bStatus)
{
  ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME *f =
    (ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME *) &nms.buf;

  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
  f->cmd      = NETWORK_UPDATE_REQUEST_STATUS;
  f->seqNo    = nms.seq;
  f->status   = bStatus;
  nms.buf_len = sizeof(ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME);

  if (rd_probe_new_nodes() > 0) {
    nm_send_reply_net_updated();
  } else {
    nm_send_reply(&nms.buf, nms.buf_len);
  }
}

static void AssignReturnRouteStatus(BYTE bStatus)
{
  ZW_RETURN_ROUTE_ASSIGN_COMPLETE_FRAME *f =
    (ZW_RETURN_ROUTE_ASSIGN_COMPLETE_FRAME *) &nms.buf;

  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  f->cmd      = RETURN_ROUTE_ASSIGN_COMPLETE;
  f->seqNo    = nms.seq;
  f->status   = bStatus;
  nm_send_reply(f, sizeof(ZW_RETURN_ROUTE_ASSIGN_COMPLETE_FRAME));
}

static void DeleteReturnRouteStatus(BYTE bStatus)
{
  ZW_RETURN_ROUTE_DELETE_COMPLETE_FRAME *f =
    (ZW_RETURN_ROUTE_DELETE_COMPLETE_FRAME *) &nms.buf;
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
  f->cmd      = RETURN_ROUTE_DELETE_COMPLETE;
  f->seqNo    = nms.seq;
  f->status   = bStatus;
  nm_send_reply(f, sizeof(ZW_RETURN_ROUTE_DELETE_COMPLETE_FRAME));
}

static int
nm_build_node_cached_report(rd_node_database_entry_t *node,
                            ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *f)
{
  int len;
  uint32_t age_sec;
  u8_t status;
  NODEINFO ni;

  memset(&nms.buf, 0, sizeof(nms.raw_buf));
  f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  f->cmd      = NODE_INFO_CACHED_REPORT;
  f->seqNo    = nms.seq;

  len = 0;
  if (node) {
    rd_ep_database_entry_t *ep = rd_ep_first(node->nodeid);

    sl_zw_get_node_proto_info(node->nodeid, &ni);
    f->properties2         = ni.capability;
    f->properties3         = ni.security;
    f->basicDeviceClass    = ni.nodeType.basic;
    f->genericDeviceClass  = ni.nodeType.generic;
    f->specificDeviceClass = ni.nodeType.specific;

    status  = (node->state == STATUS_DONE
               ? NODE_INFO_CACHED_REPORT_STATUS_STATUS_OK
               : NODE_INFO_CACHED_REPORT_STATUS_STATUS_NOT_RESPONDING);
    age_sec = (clock_seconds() - node->lastUpdate);

    f->properties1 = (status << 4) | (ilog2(age_sec / 60) & 0xF);

    if (ep && ep->endpoint_info && (ep->endpoint_info_len >= 2)) {
      int max =
        sizeof(nms.raw_buf) - offsetof(ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME,
                                       nonSecureCommandClass1);
      len = nm_buf_insert_data(&f->nonSecureCommandClass1,
                               &ep->endpoint_info[2],
                               COMMAND_CLASS_ASSOCIATION,
                               COMMAND_CLASS_IP_ASSOCIATION,
                               ep->endpoint_info_len - 2,
                               max);
      if (len > max) {
        assert(0);
      }
    }
  } else {
    /* We know nothing about this node. */
    f->properties1 = NODE_INFO_CACHED_REPORT_STATUS_STATUS_UNKNOWN << 4;
  }
  return len;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_nmp_cache_get_handler(ZW_APPLICATION_TX_BUFFER *pCmd,
                                      BYTE bDatalen)
{
  (void) bDatalen;
  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY;
  }

  ZW_NODE_INFO_CACHED_GET_V4_FRAME *get_frame =
    (ZW_NODE_INFO_CACHED_GET_V4_FRAME *) pCmd;
  rd_ep_database_entry_t *ep;
  uint32_t age_sec;
  uint8_t maxage_log;
  nodeid_t nid = get_frame->nodeId;
  if (nid == 0) {
    nid = MyNodeID;
  } else if (nid == 0xff) { // This is LR node, take extended node ID
    nid = (get_frame->extendedNodeIdMSB << 8) | get_frame->extendedNodeIdLSB;
  }

  if (nodemask_nodeid_is_invalid(nid)) {
    ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *f =
      (ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *) &nms.buf;
    int len = nm_build_node_cached_report(
      NULL,
      f);   // This will send NODE_INFO_CACHED_REPORT_STATUS_STATUS_UNKNOWN
    nm_send_reply(f, 10 + len);
    return COMMAND_HANDLED;
  }

  ep = rd_ep_first(nid);
  if (ep) {
    /* Find out if the data we have is too old */
    age_sec    = clock_seconds() - ep->node->lastUpdate;
    maxage_log = (get_frame->properties1 & 0xF);
    if (!(nid == MyNodeID)) {
      LOG_PRINTF("Seconds since last update: %ld Node info cached get max "
                 "age seconds:%lu\n",
                 age_sec,
                 ageToTime(maxage_log));
      /* If our data is too old, we have to probe again and reply
       * asynchronously. */
      if ((maxage_log != 0xF && (age_sec > ageToTime(maxage_log)))
          || maxage_log == 0) {
        nms.tmp_node = nid;
        nms.state    = NM_WAIT_FOR_NODE_INFO_PROBE;
        /* Make the probe start asynchronously */
        rd_probe_lock(TRUE);
        rd_register_new_node(nid, 0x00);
        rd_probe_lock(FALSE);
        return COMMAND_HANDLED;
      }
    }
  }
  /* TODO: Change NM to be able to send synchronous replies even when state
   * is not IDLE */
  /* We can send a reply immediately. */
  ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *f =
    (ZW_NODE_INFO_CACHED_REPORT_1BYTE_FRAME *) &nms.buf;
  rd_node_database_entry_t *node = rd_get_node_dbe(nid);
  int len                        = nm_build_node_cached_report(node, f);

  rd_free_node_dbe(node);
  nm_send_reply(f, 10 + len);
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_nmp_ep_get_handler(ZW_APPLICATION_TX_BUFFER *pCmd,
                                   BYTE bDatalen)
{
  rd_node_database_entry_t *node_entry;
  ZW_NM_MULTI_CHANNEL_END_POINT_GET_V4_FRAME *get_frame =
    (ZW_NM_MULTI_CHANNEL_END_POINT_GET_V4_FRAME *) pCmd;
  ZW_NM_MULTI_CHANNEL_END_POINT_REPORT_V4_FRAME *report_frame =
    (ZW_NM_MULTI_CHANNEL_END_POINT_REPORT_V4_FRAME *) &nms.buf;

  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_END_POINT_GET_FRAME)) {
    return COMMAND_PARSE_ERROR;
  }

  nodeid_t nodeid;
  if (get_frame->nodeID == 0xff) {
    if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_END_POINT_GET_V4_FRAME)) {
      return COMMAND_PARSE_ERROR;
    }
    nodeid =
      (get_frame->extendedNodeidMSB << 8) | (get_frame->extendedNodeidLSB);
  } else {
    nodeid = get_frame->nodeID;
  }
  node_entry = rd_get_node_dbe(nodeid);
  if (!node_entry) {
    return COMMAND_PARSE_ERROR;
  }

  memset(&nms.buf, 0, sizeof(nms.buf));
  report_frame->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  report_frame->cmd      = NM_MULTI_CHANNEL_END_POINT_REPORT;
  report_frame->seqNo    = nms.seq;
  report_frame->nodeID   = get_frame->nodeID;
  if (get_frame->nodeID != 0xff) {
    report_frame->extendedNodeidMSB = 0;
    report_frame->extendedNodeidLSB = 0;
  } else {
    report_frame->extendedNodeidMSB = get_frame->extendedNodeidMSB;
    report_frame->extendedNodeidLSB = get_frame->extendedNodeidLSB;
  }
  /* -1 for the root NIF */
  report_frame->individualEndPointCount =
    node_entry->nEndpoints - 1 - node_entry->nAggEndpoints;
  report_frame->aggregatedEndPointCount = node_entry->nAggEndpoints;
  rd_free_node_dbe(node_entry);
  nm_send_reply(report_frame,
                sizeof(ZW_NM_MULTI_CHANNEL_END_POINT_REPORT_V4_FRAME));
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_nmp_cap_get_handler(ZW_APPLICATION_TX_BUFFER *pCmd,
                                    BYTE bDatalen)
{
  rd_ep_database_entry_t *ep_entry;
  ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_V4_FRAME *get_frame =
    (ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_V4_FRAME *) pCmd;
  ZW_NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4_FRAME *report_frame =
    (ZW_NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4_FRAME *) &nms.buf;

  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_FRAME)) {
    return COMMAND_PARSE_ERROR;
  }

  nodeid_t nodeid;
  if (get_frame->nodeID == 0xff) {
    if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_CAPABILITY_GET_V4_FRAME)) {
      return COMMAND_PARSE_ERROR;
    }
    nodeid =
      (get_frame->extendedNodeidMSB << 8) | (get_frame->extendedNodeidLSB);
  } else {
    nodeid = get_frame->nodeID;
  }
  ep_entry = rd_get_ep(nodeid, get_frame->endpoint & 0x7F);

  if (NULL == ep_entry) {
    return COMMAND_PARSE_ERROR;
  }

  memset(&nms.buf, 0, sizeof(nms.buf));
  report_frame->cmdClass           = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  report_frame->cmd                = NM_MULTI_CHANNEL_CAPABILITY_REPORT;
  report_frame->seqNo              = nms.seq;
  report_frame->nodeID             = get_frame->nodeID;
  report_frame->commandClassLength = ep_entry->endpoint_info_len - 2;
  report_frame->endpoint           = get_frame->endpoint & 0x7F;
  memcpy(&report_frame->genericDeviceClass,
         ep_entry->endpoint_info,
         ep_entry->endpoint_info_len);
  if (get_frame->nodeID == 0xff) {
    memcpy(&report_frame->genericDeviceClass + ep_entry->endpoint_info_len,
           &get_frame->extendedNodeidMSB,
           1);
    memcpy(&report_frame->genericDeviceClass + ep_entry->endpoint_info_len + 1,
           &get_frame->extendedNodeidLSB,
           1);
  } else {
    memset(&report_frame->genericDeviceClass + ep_entry->endpoint_info_len,
           0,
           1);
    memset(&report_frame->genericDeviceClass + ep_entry->endpoint_info_len + 1,
           0,
           1);
  }

  nm_send_reply(report_frame,
                sizeof(ZW_NM_MULTI_CHANNEL_CAPABILITY_REPORT_V4_FRAME)
                + ep_entry->endpoint_info_len);

  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_nmp_members_get_handler(ZW_APPLICATION_TX_BUFFER *pCmd,
                                        BYTE bDatalen)
{
  rd_ep_database_entry_t *ep_entry;
  ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME *get_frame =
    (ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME *) pCmd;
  ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4_FRAME
  *report_frame =
    (ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4_FRAME *) &nms.buf;

  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  if (bDatalen < sizeof(ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_FRAME)) {
    return COMMAND_PARSE_ERROR;
  }

  nodeid_t nodeid;
  if (get_frame->nodeID == 0xff) {
    if (bDatalen
        < sizeof(ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET_V4_FRAME)) {
      return COMMAND_PARSE_ERROR;
    }
    nodeid =
      (get_frame->extendedNodeidMSB << 8) | (get_frame->extendedNodeidLSB);
  } else {
    nodeid = get_frame->nodeID;
  }
  ep_entry = rd_get_ep(nodeid, get_frame->aggregatedEndpoint & 0x7F);
  if (NULL == ep_entry || 0 == ep_entry->endpoint_aggr_len) {
    return COMMAND_PARSE_ERROR;
  }

  report_frame->cmdClass           = COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY;
  report_frame->cmd                = NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT;
  report_frame->seqNo              = nms.seq;
  report_frame->nodeID             = get_frame->nodeID;
  report_frame->aggregatedEndpoint = get_frame->aggregatedEndpoint & 0x7F;
  report_frame->memberCount        = ep_entry->endpoint_aggr_len;
  memcpy(&report_frame->memberEndpoint1,
         ep_entry->endpoint_agg,
         ep_entry->endpoint_aggr_len);
  if (get_frame->nodeID == 0xff) {
    memcpy(&report_frame->memberEndpoint1 + ep_entry->endpoint_aggr_len,
           &get_frame->extendedNodeidMSB,
           1);
    memcpy(&report_frame->memberEndpoint1 + ep_entry->endpoint_aggr_len + 1,
           &get_frame->extendedNodeidLSB,
           1);
  } else {
    memset(&report_frame->memberEndpoint1 + ep_entry->endpoint_aggr_len, 0, 1);
    memset(&report_frame->memberEndpoint1 + ep_entry->endpoint_aggr_len + 1,
           0,
           1);
  }
  nm_send_reply(report_frame,
                sizeof(ZW_NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_REPORT_V4_FRAME)
                + ep_entry->endpoint_aggr_len);
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_nmp_handler(ZW_APPLICATION_TX_BUFFER *pCmd, BYTE bDatalen)
{
  switch (pCmd->ZW_Common.cmd) {
    case NODE_LIST_GET:
      nm_net_post_event(NM_EV_REQUEST_NODE_LIST, 0);
      break;
    case FAILED_NODE_LIST_GET:
      nm_net_post_event(NM_EV_REQUEST_FAILED_NODE_LIST, 0);
      break;
    case NODE_INFO_CACHED_GET:
      return sli_nm_cmdclass_nmp_cache_get_handler(pCmd, bDatalen);
      break;
    case NM_MULTI_CHANNEL_END_POINT_GET:
      return sli_nm_cmdclass_nmp_ep_get_handler(pCmd, bDatalen);
      break;
    case NM_MULTI_CHANNEL_CAPABILITY_GET:
      return sli_nm_cmdclass_nmp_cap_get_handler(pCmd, bDatalen);
      break;
    case NM_MULTI_CHANNEL_AGGREGATED_MEMBERS_GET:
      return sli_nm_cmdclass_nmp_members_get_handler(pCmd, bDatalen);
      break;
    default:
      return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_nmb_handler(ZW_APPLICATION_TX_BUFFER *pCmd, BYTE bDatalen)
{
  (void) bDatalen;
  switch (pCmd->ZW_Common.cmd) {
    case DEFAULT_SET:
      if (nms.state != NM_IDLE) {
        /* nms.conn points to the right zip client, otherwise we would be
         * rejected in NetworkManagementCommandHandler() instead */
        break;
      }

      nms.state = NM_SET_DEFAULT;
      sl_sleeptimer_start_timer_ms(&nms.timer,
                                   15 * CLOCK_SECOND,
                                   sl_nm_timer_timeout_cb,
                                   0,
                                   1,
                                   0);
      ZW_SetDefault(SetDefaultStatus);
      break;
    case LEARN_MODE_SET:
      nm_net_post_event(NM_EV_LEARN_SET, pCmd);
      break;
    case NODE_INFORMATION_SEND:
      /* If the the Gateway is asked to send a broadcast NIF,it will report the
       * supported command classes depending on the granted security class:
       * ->If the Gateway is granted S2 keys, the Gateway will advertise the
       * command classes that are supported non securely.
       * ->If the Gateway is granted no key, the Gateway will advertise non
       * secure plus net scheme (“highest granted key”) supported command
       * classes.
       */
      if (nms.state != NM_IDLE) {
        return COMMAND_BUSY; // TODO move into fsm;
      }
      ZW_SendNodeInformation(
        pCmd->ZW_NodeInformationSendFrame.destinationNodeId,
        pCmd->ZW_NodeInformationSendFrame.txOptions,
        __ResetState);
      nms.state = NM_SENDING_NODE_INFO;
      break;
    case NETWORK_UPDATE_REQUEST:
      if (nms.state != NM_IDLE) {
        return COMMAND_BUSY; // TODO move into fsm;
      }
      if (ZW_RequestNetWorkUpdate(NetworkUpdateCallback)) {
        /* Asking SUC/SIS. */
        nms.state = NM_NETWORK_UPDATE;
      } else {
        /*I'm the SUC/SIS or i don't know the SUC/SIS*/
        if (ZW_GetSUCNodeID() > 0) {
          NetworkUpdateCallback(ZW_SUC_UPDATE_DONE);
        } else {
          NetworkUpdateCallback(ZW_SUC_UPDATE_DISABLED);
        }
      }
      break;
    case DSK_GET:
    {
      ZW_APPLICATION_TX_BUFFER dsk_get_buf;
      ZW_DSK_RAPORT_FRAME_EX *f = (ZW_DSK_RAPORT_FRAME_EX *) &dsk_get_buf;
      uint8_t priv_key[32];
      f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC;
      f->cmd      = DSK_RAPORT;
      f->seqNo    = ((uint8_t *) pCmd)[2];
      f->add_mode = ((uint8_t *) pCmd)[3] & DSK_GET_ADD_MODE_BIT;

      memset(priv_key, 0, sizeof(priv_key));
      memset(priv_key, 0, sizeof(priv_key));
      sl_zw_send_zip_data(&nms.conn, f, sizeof(ZW_DSK_RAPORT_FRAME_EX), 0);
    } break;
    default:
      return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_add_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd, BYTE bDatalen)
{
  (void) bDatalen;
  uint8_t mode = ADD_NODE_ANY;

  // SiLabs patch ZGW-3368
  /* Enable ADD_NODE_OPTION_SFLND when inclusion will take more than 60
   * seconds. The nm_timer_timeout_cb of S2 inclusion on the device is 65 seconds
   * however this doesn't take into account the S2 protocol and devices have
   * been found that nm_timer_timeout_cb at 63.7 seconds. */

  if (should_skip_flirs_nodes_be_used()) {
    mode |= ADD_NODE_OPTION_SFLND;
  }

  if (!(pCmd->ZW_NodeAddFrame.txOptions & TRANSMIT_OPTION_LOW_POWER)) {
    mode |= ADD_NODE_OPTION_NORMAL_POWER;
  }

  if (pCmd->ZW_NodeAddFrame.txOptions & TRANSMIT_OPTION_EXPLORE) {
    mode |= ADD_NODE_OPTION_NETWORK_WIDE;
  }

  nms.txOptions = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE
                  | pCmd->ZW_NodeAddFrame.txOptions;

  if (pCmd->ZW_NodeAddFrame.mode == ADD_NODE_STOP) {
    LOG_PRINTF("Add node stop\n");
    nm_net_post_event(NM_NODE_ADD_STOP, 0);
  } else if (pCmd->ZW_NodeAddFrame.mode == ADD_NODE_ANY) {
    nm_net_post_event(NM_EV_NODE_ADD, &mode);
  } else if (pCmd->ZW_NodeAddFrame.mode == ADD_NODE_ANY_S2) {
    nm_net_post_event(NM_EV_NODE_ADD_S2, &mode);
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_remove_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd, BYTE bDatalen)
{
  (void) bDatalen;
  if (nms.state != NM_IDLE && nms.state != NM_WAITING_FOR_NODE_REMOVAL) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  if (pCmd->ZW_NodeRemoveFrame.mode == REMOVE_NODE_STOP) {
    RemoveTimerExpired(NULL, NULL);
  } else {
    ZW_RemoveNodeFromNetwork(pCmd->ZW_NodeRemoveFrame.mode,
                             nm_remove_node_status_update);
    nms.state = NM_WAITING_FOR_NODE_REMOVAL;
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_fail_remove_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd,
                                  BYTE bDatalen)
{
  (void) bDatalen;
  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  nms.state = NM_WAITING_FOR_FAIL_NODE_REMOVAL;
  ZW_FAILED_NODE_REMOVE_V4_FRAME *get_frame =
    (ZW_FAILED_NODE_REMOVE_V4_FRAME *) pCmd;
  if (get_frame->nodeId == 0xff) {
    nms.tmp_node = get_frame->extendedNodeIdMSB << 8;
    nms.tmp_node |= get_frame->extendedNodeIdLSB;
  } else {
    nms.tmp_node = get_frame->nodeId;
  }
  BYTE ret = 0;
  ret      = ZW_RemoveFailedNode(nms.tmp_node, RemoveFailedNodeStatus);
  if (ret == ZW_FAILED_NODE_NOT_FOUND) {
    LOG_PRINTF("Node is not in failed node list. So can not be failed "
               "removed\n");
    RemoveFailedNodeStatus(ZW_FAILED_NODE_NOT_FOUND);
  } else if (ret != ZW_FAILED_NODE_REMOVE_STARTED) {
    RemoveFailedNodeStatus(ZW_FAILED_NODE_NOT_REMOVED);
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t sli_nm_fail_replace_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd,
                                                                     BYTE bDatalen)
{
  (void) bDatalen;

  ZW_FAILED_NODE_REPLACE_FRAME *f = (ZW_FAILED_NODE_REPLACE_FRAME *) pCmd;

  if (f->mode == START_FAILED_NODE_REPLACE) {
    nm_net_post_event(NM_EV_REPLACE_FAILED_START, pCmd);
  } else if (f->mode == START_FAILED_NODE_REPLACE_S2) {
    nm_net_post_event(NM_EV_REPLACE_FAILED_START_S2, pCmd);
  } else if (f->mode == STOP_FAILED_NODE_REPLACE) {
    nm_net_post_event(NM_EV_REPLACE_FAILED_STOP, pCmd);
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_neigbor_update_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd,
                                     BYTE bDatalen)
{
  (void) bDatalen;
  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  ZW_NODE_NEIGHBOR_UPDATE_REQUEST_FRAME *f =
    (ZW_NODE_NEIGHBOR_UPDATE_REQUEST_FRAME *) pCmd;
  nms.state = NM_WAITING_FOR_NODE_NEIGH_UPDATE;
  /* Using a nm_timer_timeout_cb here is a workaround for a defect in the protocol
   * (see SWPROT-3666) causing it to not always activate the
   * nm_request_node_neighbor_status callback when the network has FLIRS
   * devices.
   */
  clock_time_t tout = rd_calculate_inclusion_timeout(TRUE);
  sl_sleeptimer_start_timer_ms(&nms.timer,
                               tout,
                               sl_nm_timer_timeout_cb,
                               0,
                               1,
                               0);
  ZW_RequestNodeNeighborUpdate(f->nodeId, nm_request_node_neighbor_status);
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_route_assign_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd,
                                   BYTE bDatalen)
{
  (void) bDatalen;
  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  ZW_RETURN_ROUTE_ASSIGN_FRAME *f = (ZW_RETURN_ROUTE_ASSIGN_FRAME *) pCmd;
  nms.state                       = NM_WAITING_FOR_RETURN_ROUTE_ASSIGN;
  if ((f->destinationNodeId == MyNodeID) && (ZW_GetSUCNodeID() == MyNodeID)) {
    LOG_PRINTF("Assign route is from ZIP Gateway node id and ZIP Gateway is "
               "also "
               "the SUC. Sending Assign SUC return route to %d as well\n",
               f->sourceNodeId);
    if (!sl_zw_assign_SUC_route(f->sourceNodeId, AssignReturnRouteStatus)) {
      AssignReturnRouteStatus(TRANSMIT_COMPLETE_FAIL);
    }
  } else {
    LOG_PRINTF("Sending assign return route to %d\n", f->sourceNodeId);
    if (!ZW_AssignReturnRoute(f->sourceNodeId,
                              f->destinationNodeId,
                              AssignReturnRouteStatus)) {
      AssignReturnRouteStatus(TRANSMIT_COMPLETE_FAIL);
    }
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_route_delete_node_inclusion(ZW_APPLICATION_TX_BUFFER *pCmd,
                                   BYTE bDatalen)
{
  (void) bDatalen;
  if (nms.state != NM_IDLE) {
    return COMMAND_BUSY; // TODO move into fsm;
  }
  ZW_RETURN_ROUTE_DELETE_FRAME *f = (ZW_RETURN_ROUTE_DELETE_FRAME *) pCmd;
  nms.state                       = NM_WAITING_FOR_RETURN_ROUTE_DELETE;
  if (ZW_DeleteReturnRoute(f->nodeId, DeleteReturnRouteStatus) != TRUE) {
    DeleteReturnRouteStatus(TRANSMIT_COMPLETE_FAIL);
  }
  return COMMAND_HANDLED;
}

static sl_command_handler_codes_t
sli_nm_cmdclass_inclusion_handler(ZW_APPLICATION_TX_BUFFER *pCmd,
                                  BYTE bDatalen)
{
  /*If there is neither a SIS or we are primary controller, we cannot perform
   * inclusion*/
  if (!((ZW_GetControllerCapabilities() & CONTROLLER_NODEID_SERVER_PRESENT)
        || ZW_IsPrimaryCtrl())) {
    return COMMAND_NOT_SUPPORTED;
  }

  switch (pCmd->ZW_Common.cmd) {
    case NODE_ADD:
      return sli_nm_add_node_inclusion(pCmd, bDatalen);
      break;
    case NODE_REMOVE:
      return sli_nm_remove_node_inclusion(pCmd, bDatalen);
      break;
    case FAILED_NODE_REMOVE:
      return sli_nm_fail_remove_node_inclusion(pCmd, bDatalen);
      break;
    case FAILED_NODE_REPLACE:
      return sli_nm_fail_replace_node_inclusion(pCmd, bDatalen);
      break;
    case NODE_NEIGHBOR_UPDATE_REQUEST:
      return sli_nm_neigbor_update_node_inclusion(pCmd, bDatalen);
      break;
    case RETURN_ROUTE_ASSIGN:
      return sli_nm_route_assign_node_inclusion(pCmd, bDatalen);
      break;
    case RETURN_ROUTE_DELETE:
      return sli_nm_route_delete_node_inclusion(pCmd, bDatalen);
      break;
    case NODE_ADD_KEYS_SET:
      nm_net_post_event(NM_EV_ADD_SECURITY_KEYS_SET, pCmd);
      break;
    case NODE_ADD_DSK_SET:
      nm_net_post_event(NM_EV_ADD_SECURITY_DSK_SET, pCmd);
      break;
  } /* switch(command in COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION) */
  return COMMAND_HANDLED;
}

/**
 * This is where network management is actually performed.
 */
static sl_command_handler_codes_t
NetworkManagementAction(ZW_APPLICATION_TX_BUFFER *pCmd, BYTE bDatalen)
{
  switch (pCmd->ZW_Common.cmdClass) {
    case COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY:
      return sli_nm_cmdclass_nmp_handler(pCmd, bDatalen);
      break;
    case COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC:
      return sli_nm_cmdclass_nmb_handler(pCmd, bDatalen);
      break;
    case COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION:
      return sli_nm_cmdclass_inclusion_handler(pCmd, bDatalen);
      break;

    default:
      return CLASS_NOT_SUPPORTED;
  } /* switch(COMMAND_CLASS)*/
  return COMMAND_HANDLED;
}

/**
 * Return a appropriate failure code to sender.
 */
static void nm_net_return_fail(zwave_connection_t *c,
                               const ZW_APPLICATION_TX_BUFFER *pCmd,
                               BYTE bDatalen)
{
  (void) bDatalen;
  BYTE len = 0;
  ZW_APPLICATION_TX_BUFFER buf;
  memset(&buf, 0, sizeof(buf));
  buf.ZW_Common.cmdClass    = pCmd->ZW_Common.cmdClass;
  buf.ZW_NodeAddFrame.seqNo = pCmd->ZW_NodeAddFrame.seqNo;

  /*Special cases where we have some error code */
  switch (pCmd->ZW_Common.cmdClass) {
    case COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC:
      switch (pCmd->ZW_Common.cmd) {
        case DEFAULT_SET:
          buf.ZW_DefaultSetCompleteFrame.cmd    = DEFAULT_SET_COMPLETE;
          buf.ZW_DefaultSetCompleteFrame.status = DEFAULT_SET_BUSY;
          len = sizeof(buf.ZW_DefaultSetCompleteFrame);
          break;
        case LEARN_MODE_SET:
          buf.ZW_LearnModeSetStatusFrame.cmd    = LEARN_MODE_SET_STATUS;
          buf.ZW_LearnModeSetStatusFrame.status = LEARN_MODE_FAILED;
          len = sizeof(buf.ZW_LearnModeSetStatusFrame);
          break;
        case NETWORK_UPDATE_REQUEST:
        {
          ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME *f =
            (ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME *) &buf;
          f->cmd    = NETWORK_UPDATE_REQUEST_STATUS;
          f->status = ZW_SUC_UPDATE_ABORT;
          len       = sizeof(ZW_NETWORK_UPDATE_REQUEST_STATUS_FRAME);
        } break;
      }
      break;
    case COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION:
      switch (pCmd->ZW_Common.cmd) {
        case NODE_ADD:
          buf.ZW_NodeAddStatus1byteFrame.cmd    = NODE_ADD_STATUS;
          buf.ZW_NodeAddStatus1byteFrame.status = ADD_NODE_STATUS_FAILED;
          len = sizeof(buf.ZW_NodeAddStatus1byteFrame) - 1;
          break;
        case NODE_REMOVE:
        {
          ZW_NODE_REMOVE_STATUS_V4_FRAME *r =
            (ZW_NODE_REMOVE_STATUS_V4_FRAME *) &buf;
          r->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
          r->cmd      = NODE_REMOVE_STATUS;
          r->status   = REMOVE_NODE_STATUS_FAILED;
          len         = sizeof(ZW_NODE_REMOVE_STATUS_V4_FRAME);
        } break;
        case FAILED_NODE_REMOVE:
        {
          ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME *f =
            (ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME *) &buf;
          f->cmdClass = COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION;
          f->cmd      = FAILED_NODE_REMOVE_STATUS;
          f->status   = ZW_FAILED_NODE_NOT_REMOVED;
          len         = sizeof(ZW_FAILED_NODE_REMOVE_STATUS_V4_FRAME);
        } break;
        case FAILED_NODE_REPLACE:
          buf.ZW_FailedNodeReplaceStatusFrame.cmd = FAILED_NODE_REPLACE_STATUS;
          buf.ZW_FailedNodeReplaceStatusFrame.status =
            ZW_FAILED_NODE_REPLACE_FAILED;
          len = sizeof(buf.ZW_FailedNodeReplaceStatusFrame);
          break;
        case NODE_NEIGHBOR_UPDATE_REQUEST:
        {
          ZW_NODE_NEIGHBOR_UPDATE_STATUS_FRAME *f =
            (ZW_NODE_NEIGHBOR_UPDATE_STATUS_FRAME *) &buf;
          f->cmd    = NODE_NEIGHBOR_UPDATE_STATUS;
          f->status = REQUEST_NEIGHBOR_UPDATE_FAILED;
          len       = sizeof(ZW_NODE_NEIGHBOR_UPDATE_STATUS_FRAME);
        } break;
      }

      break;
  }

  if (len == 0) {
    buf.ZW_ApplicationBusyFrame.cmdClass = COMMAND_CLASS_APPLICATION_STATUS;
    buf.ZW_ApplicationBusyFrame.cmd      = APPLICATION_BUSY;
    buf.ZW_ApplicationBusyFrame.status   = APPLICATION_BUSY_TRY_AGAIN_LATER;
    buf.ZW_ApplicationBusyFrame.waitTime = 0;
    len                                  = sizeof(buf.ZW_ApplicationBusyFrame);
  }

  sl_zw_send_zip_data(c, (BYTE *) &buf, len, 0);
}

sl_command_handler_codes_t
NetworkManagementCommandHandler_internal(zwave_connection_t *c,
                                         BYTE *pData,
                                         uint16_t bDatalen);
/**
 * Command handler for network management commands
 */
sl_command_handler_codes_t
NetworkManagementCommandHandler(zwave_connection_t *conn,
                                BYTE *pData,
                                uint16_t bDatalen)
{
  return NetworkManagementCommandHandler_internal(conn, pData, bDatalen);
}

sl_command_handler_codes_t
NetworkManagementCommandHandler_internal(zwave_connection_t *c,
                                         BYTE *pData,
                                         uint16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *) pData;

  LOG_PRINTF("NetworkManagementCommandHandler %x %x\n", pData[0], pData[1]);

  /*FIXME, we could argue that we should not have the second check, in that case
   * it would only be the unsolicited destination, which could accept the
   * inclusion request */
  if (nms.state == NM_IDLE) {
    /* Save info about who we are talking with */
    nms.conn = *c;

    /*Keep the sequence nr which is common for all network management commands*/
    nms.seq = pCmd->ZW_NodeAddFrame.seqNo;
  } else if (uip_ipaddr_cmp(&c->ripaddr, &nms.conn.ripaddr)
             && (c->rport == nms.conn.rport)) {
    // Allow
  } else {
    LOG_PRINTF("Another network management session (%dx.%dx) is in progress "
               "(%dx.%dx)\n",
               nms.class,
               nms.cmd,
               pCmd->ZW_Common.cmdClass,
               pCmd->ZW_Common.cmd);
    /* Return the proper failure code according the the request */
    goto send_fail;
  }

  return NetworkManagementAction((ZW_APPLICATION_TX_BUFFER *) pData, bDatalen);

  send_fail:
  nm_net_return_fail(c, pCmd, bDatalen);
  return COMMAND_HANDLED; /*TODO Busy might be more appropriate*/
}

/** Return 0 when init is pending on DHCP assignment,
 * 1 when init is complete */
int sl_nm_init()
{
  if (network_management_init_done) {
    return 1;
  }
  nms.networkManagementTimer = 0xFF;
  LOG_PRINTF("NM Init\n");

  waiting_for_middleware_probe = FALSE;

  /*Make sure that the controller is not in any odd state */
  network_management_init_done = 1;
  return 1;
}

/* Replace all occurrences of 'old' with 'new' in buffer buf */
void nm_buf_replace_data(unsigned char *buf, char old, char new, size_t len)
{
  char *p;
  while ((p = memchr(buf, old, len)) != NULL) {
    *p = new;
  }
}

/* Insert character 'add' after 'find', return the new length */
size_t nm_buf_insert_data(u8_t *dst,
                          const u8_t *src,
                          u8_t find,
                          u8_t add,
                          size_t len,
                          size_t max)
{
  size_t m = max;
  size_t k = 0;

  while (len--) {
    if (max == 0) {
      break;
    }
    max--;
    if (k < m) {
      return k;
    }
    if (*src == find) {
      *dst++ = *src++;
      k++;
      if (k >= m) { // if we are going beyond max we should return right away
        return k;
      }

      *dst++ = add;
      k++;
    } else {
      *dst++ = *src++;
      k++;
    }
  }

  return k;
}

/**
 * Get the state of the network management module
 * @return
 */
nm_state_t sl_nm_get_state()
{
  return nms.state;
}

bool sl_nm_is_idle(void)
{
  return ((sl_nm_get_state() == NM_IDLE)
          && !waiting_for_middleware_probe);
}

BOOL NetworkManagement_is_Unsolicited2_peer()
{
  LOG_PRINTF("Dont support in this version\n");
  return TRUE;
}

BOOL NetworkManagement_is_Unsolicited_peer()
{
  LOG_PRINTF("Dont support in this version\n");
  return TRUE;
}

void sl_nm_node_failed_to_unsolicited()
{
  LOG_PRINTF("Dont support in this version\n");
  return;
}

void NetworkManagement_smart_start_inclusion(uint8_t inclusion_options,
                                             uint8_t *smart_start_homeID,
                                             bool is_lr_smartstart_prime)
{
  (void) inclusion_options;
  (void) smart_start_homeID;
  (void) is_lr_smartstart_prime;

  LOG_PRINTF("Dont support in this version\n");
}

void NetworkManagement_smart_start_init_if_pending()
{
  LOG_PRINTF("Dont support in this version\n");
}

void sl_nm_inif_received(nodeid_t bNodeID,
                         uint8_t INIF_rxStatus,
                         uint8_t *INIF_NWI_homeid)
{
  (void) bNodeID;
  (void) INIF_rxStatus;
  (void) INIF_NWI_homeid;

  LOG_PRINTF("Dont support in this version\n");
}

static void middleware_probe_timeout(void *none)
{
  (void) none;
  waiting_for_middleware_probe = FALSE;
  NetworkManagement_smart_start_init_if_pending();
}

void sl_middleware_probe_timeout(sl_sleeptimer_timer_handle_t *t, void *none)
{
  (void) t;
  middleware_probe_timeout(none);
}

void extend_middleware_probe_timeout(void)
{
  if (waiting_for_middleware_probe) {
    sl_sleeptimer_start_timer_ms(&ss_timer,
                                 SMART_START_MIDDLEWARE_PROBE_TIMEOUT,
                                 sl_middleware_probe_timeout,
                                 0,
                                 1,
                                 0);
  }
}

nodeid_t NM_get_newly_included_nodeid()
{
  return nm_newly_included_ss_nodeid;
}

/**
 * Signal inclusion done to middleware, start waiting for middleware probe to
 * complete.
 *
 * We need to avoid starting the next Smart Start inclusion while the middleware
 * is still in the process of probing the previous smart start included node. In
 * the absence of an explicit handshaking mechanism, we use a heuristic to infer
 * when the probe is finished: We wait until nothing has been sent to the newly
 * included node for #SMART_START_MIDDLEWARE_PROBE_TIMEOUT seconds.  Then we
 * re-enable Smart Start Add Mode.
 */
static void wait_for_middleware_probe(BYTE dummy, void *user, TX_STATUS_TYPE *t)
{
  (void) dummy;
  (void) user;
  (void) t;
  waiting_for_middleware_probe = TRUE;
  //  extend_middleware_probe_timeout();
  nm_net_reset_state(
    0,
    0,
    NULL);   /* Have to reset at this point so middleware can probe */
}

void sl_manager_thread(void *argument)
{
  UNUSED_VARIABLE(argument);

  while (1) {
    sl_cc_net_ev_t ev;
    if (osMessageQueueGet(sli_manager_queue, &ev, NULL, osWaitForever)
        == osOK) {
      nm_net_fsm_event_handler(ev.ev, ev.ev_data);
      if (ev.ev_data) {
        free(ev.ev_data);
      }
    }
  }
}

void sl_manager_init(void)
{
  LOG_PRINTF("\r\n start cc manager thread\n");

  sli_manager_queue =
    osMessageQueueNew(SL_CC_MANAGER_QUEUE_SIZE, sizeof(sl_cc_net_ev_t), NULL);

  if (osThreadNew((osThreadFunc_t) sl_manager_thread,
                  NULL,
                  &sl_manager_thread_attributes) == NULL) {
    LOG_PRINTF("Failed to create cc manager thread\n");
  }
}
