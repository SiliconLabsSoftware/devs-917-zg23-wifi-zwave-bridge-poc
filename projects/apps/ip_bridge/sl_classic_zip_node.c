/*******************************************************************************
 * @file  sl_classic_zip_node.c
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

#include "sl_classic_zip_node.h"
#include "Serialapi.h"
#include "ZW_udp_server.h"
#include "Net/ZW_zip_classcmd.h"
#include <ZW_classcmd_ex.h>
#include "modules/sl_rd_data_store.h"
#include "transport/sl_zw_send_data.h"
#include "utls/ipv6_utils.h"
#include "sl_common_log.h"
#include "sl_common_config.h"
#include "ZW_nodemask_api.h"
#include "ip_translate/multicast_tlv.h"
#include "ip_translate/sl_zw_resource.h"

#include "utls/sl_ipnode_utils.h"

#include "sl_bridge_ip_assoc.h"
#include "sl_bridge_temp_assoc.h"

#define MANGLE_MAGIC 0x55AA

#define MAX_CLASSIC_SESSIONS ZW_MAX_NODES /* Only used for non-bridge libraries */
#define FAIL_TIMEOUT_TEMP_ASSOC 2000 /* Time to wait for ZW_SendData() callback on unsolicited,
                                      * incoming frames before we give up. */
/* 10 sec delay, from SDS11402 */
#define CLASSIC_SESSION_TIMEOUT 65000UL

/* 10 sec delay, from SDS11402 */
const BYTE ZW_NOP[] =
{ 0 };

uint8_t backup_buf[UIP_MAX_FRAME_BYTES];
uint16_t backup_len;
uint8_t is_device_reset_locally = 0;
uint16_t zip_payload_len;

static uint8_t classic_txBuf[UIP_MAX_FRAME_BYTES - UIP_IPUDPH_LEN];/*NOTE: the local txBuf is ok here, because the frame is sent via the SerialAPI*/

static uint8_t txOpts;

static BOOL bSendNAck;

sl_sleeptimer_timer_handle_t nak_wait_timer;

/* Place holders for parameters needed to send ACK, in the ZW_SendData callback */

zwave_connection_t sl_zwc;

static BYTE cur_flags0;
static BYTE cur_flags1;

static uint8_t cur_SendDataAppl_handle;

static VOID_CALLBACKFUNC(cbCompletedFunc)(BYTE, BYTE * data, uint16_t len);

static BOOL
proxy_command_handler(zwave_connection_t* c, const uint8_t* payload, uint8_t len, BOOL was_dtls, BOOL ack_req, uint8_t bSupervisionUnwrapped);

static int
LogicalRewriteAndSend();

/**
 * Wrapper for sl_zw_send_data_appl() that saves result to private handle in ClassicZipNode module.
 *
 * For description of parameters and return value see sl_zw_send_data_appl().
 */
uint8_t ClassicZIPNode_SendDataAppl(ts_param_t* p,
                                    const void *pData,
                                    uint16_t dataLength,
                                    ZW_SendDataAppl_Callback_t callback,
                                    void* user)
{
  cur_SendDataAppl_handle = sl_zw_send_data_appl(p, pData, dataLength, callback, user);
  return cur_SendDataAppl_handle;
}

void
ClassicZIPNode_CallSendCompleted_cb(BYTE bStatus, void* usr, TX_STATUS_TYPE *t)
{
  (void) usr;
  (void) t;
  uint16_t l;
  VOID_CALLBACKFUNC(tmp_cbCompletedFunc)(BYTE, BYTE * data, uint16_t len);

  l = bkp_zip_len;
  backup_len = 0;
  cur_SendDataAppl_handle = 0;
  tmp_cbCompletedFunc = cbCompletedFunc;
  cbCompletedFunc = NULL;
  if (tmp_cbCompletedFunc) {
    tmp_cbCompletedFunc(bStatus, (BYTE*)bkp_zip, l);
  }
}

void
report_send_completed(uint8_t bStatus)
{
  ClassicZIPNode_CallSendCompleted_cb(bStatus, NULL, NULL);
  sl_sleeptimer_stop_timer(&nak_wait_timer);
}

/**
 * Send Ack or nack to from srcNode to dst_ip(NOTE this write directly to uip_buf)
 * If ack is true send a ack otherwise send a nak , if wait is true also mark wait.
 * This is an extended version of SendUDPStatus which also sends RSSI info.
 *
 * \param is_mcast Flag to indicate if this ack should contain multicast statuc TLV
 */
void SendUDPStatus_rssi(int flags0, int flags1, zwave_connection_t *c, TX_STATUS_TYPE *txStat, BOOL is_mcast)
{
  (void) txStat;
  (void) is_mcast;

  uint16_t len;
  uint8_t buf[40];

  ZW_COMMAND_ZIP_PACKET *zip = (ZW_COMMAND_ZIP_PACKET *)buf;

  zip->cmdClass = COMMAND_CLASS_ZIP;
  zip->cmd = COMMAND_ZIP_PACKET;

  zip->flags0 = flags0;
  zip->flags1 = flags1;
  zip->seqNo = c->seq;
  zip->sEndpoint = c->rendpoint;
  zip->dEndpoint = c->lendpoint;

  len = ZIP_HEADER_SIZE;
  zip->payload[0] = 0;  /* Filled when header is complete */
  zip->payload[1] = 0x80 | ENCAPSULATION_FORMAT_INFO;

  len += 2; /* 2 bytes for EFI */

  udp_send_wrap(&c->conn, (const uint8_t*)zip, len, 0, 0);
}

/*
 * Send Ack or nack to from srcNode to dst_ip(NOTE this write directly to uip_buf)
 * If ack is true send a ack otherwise send a nak , if wait is true also mark wait
 */
void SendUDPStatus(int flags0, int flags1, zwave_connection_t *c)
{
  (void) flags0;
  (void) flags1;
  (void) c;
  SendUDPStatus_rssi(flags0, flags1, c, NULL, FALSE);
}

static void
nak_wait_timeout(void* user)
{
  (void) user;
  SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_WAIT | ZIP_PACKET_FLAGS0_NACK_RES, cur_flags1, &sl_zwc);
}

void sl_nak_timer_timeout(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) t;
  nak_wait_timeout(u);
}

/**
 * Callback from send_using_temp_assoc
 *
 * Send a ZIP Ack/Nack
 *
 * @param bStatus Transmission status
 * @param user HAN node id of destination node
 * @param t pointer to TX status
 * @param is_mcast TRUE if ClassicZIPNode_SendDataAppl() was sending a multicast
 *                 frame, FALSE otherwise
 */
static void
send_using_temp_assoc_callback_ex(BYTE bStatus, void *user, TX_STATUS_TYPE *t, BOOL is_mcast)
{
  /* NB: The "user" pointer is NOT pointing to a memory location with the node
   * id. The actual pointer value IS the node id (saves us from creating a
   * static/global variable to hold the node id).
   */
  nodeid_t dest_nodeid = (nodeid_t) (intptr_t) user;

  DBG_PRINTF("send_using_temp_assoc_callback_ex for node %d status %u\n", dest_nodeid, bStatus);

  if ((bStatus == TRANSMIT_COMPLETE_NO_ACK) /*&& (get_queue_state() == QS_SENDING_FIRST)*/) {
    /* Do not send an ACK/NACK because the frame will be re-queued in the long queue*/
  } else {
    /* Only send ACK if it has been requested */
    if ( (cur_flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) ) {
      int flags0 = (bStatus == TRANSMIT_COMPLETE_OK) ? ZIP_PACKET_FLAGS0_ACK_RES
                   : ZIP_PACKET_FLAGS0_NACK_RES;
      SendUDPStatus_rssi(flags0, cur_flags1, &sl_zwc, t, is_mcast);
    }

    if (bStatus == TRANSMIT_COMPLETE_OK) {
      rd_node_is_alive(dest_nodeid);
    } else {
      rd_node_is_unreachable(dest_nodeid);
    }
  }

  report_send_completed(bStatus);
}

/* Callback from send_using_temp_assoc on single-cast frame */
static void
send_using_temp_assoc_callback(BYTE bStatus, void* user, TX_STATUS_TYPE *t)
{
  send_using_temp_assoc_callback_ex(bStatus, user, t, FALSE);
}

/* Callback from senddata after forwarding a frame through a proxy IP
 * Association.
 */
static void
forward_to_ip_assoc_proxy_callback(BYTE bStatus, void* user, TX_STATUS_TYPE *t)
{
  (void) t;
  ip_association_t *a = (ip_association_t*)user;
  DBG_PRINTF("forward_to_ip_assoc_proxy_callback status %u, node %u\n", bStatus, a->han_nodeid);

  if (bStatus == TRANSMIT_COMPLETE_OK) {
    rd_node_is_alive(a->han_nodeid);
  }

  report_send_completed(bStatus);
}

/**
 * Send a ZIP Packet with ACK/NACK status on association processing.
 *
 * \note This function uses TRANSMIT_COMPLETE_OK (==0) to indicate success.
 *
 * \param bStatus Status. TRANSMIT_COMPLETE_OK if successful, TRANSMIT_COMPLETE_FAIL if failed.
 */
void
send_zip_ack(uint8_t bStatus)
{
  if (bStatus == TRANSMIT_COMPLETE_OK) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_ACK_RES, cur_flags1, &sl_zwc);
  } else if (bSendNAck) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES, cur_flags1, &sl_zwc);
  }
}

/**
 * Send frame in classic_txBuf using temporary association virtual node
 *
 * @param a temporary association to use
 */
static void
send_using_temp_assoc(temp_association_t *a)
{
  /* Extract the Z-Wave destination nodeid and endpoint */
  nodeid_t han_nodeid = sl_node_of_ip(&(sl_uip_buf_dst_addr()));
  uint8_t  han_endpoint = sl_uip_buf_rendpoint();

  ts_param_t p = {};
  uint8_t h = 0;

  ts_set_std(&p, han_nodeid);
  p.tx_flags = ClassicZIPNode_getTXOptions();
  p.is_mcast_with_folloup = cur_flags0 & ZIP_PACKET_FLAGS0_ACK_REQ ? TRUE : FALSE;
  p.dendpoint = han_endpoint;
  p.sendpoint = sl_zwc.rendpoint;
  if (is_device_reset_locally) {
    p.snode = MyNodeID;
  } else {
    p.snode = a->virtual_id;
  }
  p.scheme = sl_zwc.scheme;
  LOG_PRINTF("send_using_temp_assoc info: d_ed=%d, s_ed=%d, sch=%d\n", p.dendpoint, p.sendpoint, p.scheme);

  h = ClassicZIPNode_SendDataAppl(&p,
                                  classic_txBuf,
                                  zip_payload_len,
                                  send_using_temp_assoc_callback,
                                  (void *) (intptr_t) han_nodeid);

  if (h) {
    // just call immediately
    LOG_PRINTF("Send ack/nack immediately\n");
    sl_nak_timer_timeout(NULL, NULL);
  } else {
    ERR_PRINTF("sl_zw_send_data_appl() failed\n");
    send_using_temp_assoc_callback(TRANSMIT_COMPLETE_FAIL,
                                   (void *) (intptr_t) han_nodeid,
                                   NULL);
  }
}

/**
 * Intercept version command class get for the ZIP naming class and the ip association class.
 */
static uint8_t
handle_version(const uint8_t* payload, uint8_t len, BOOL ack_req)
{
  int the_version;
  ZW_VERSION_COMMAND_CLASS_REPORT_FRAME f;

  if (len < sizeof(ZW_VERSION_COMMAND_CLASS_GET_FRAME)) {
    return FALSE;
  }

  if (payload[1] == VERSION_COMMAND_CLASS_GET) {
    the_version = 0;

    if (payload[2] == COMMAND_CLASS_ZIP_NAMING) {
      the_version = ZIP_NAMING_VERSION;
    }
    if (payload[2] == COMMAND_CLASS_IP_ASSOCIATION) {
      the_version = IP_ASSOCIATION_VERSION;
    }
#ifdef TEST_MULTICAST_TX
    if (payload[2] == COMMAND_CLASS_ZIP) {
      the_version = ZIP_VERSION_V5;
    }
#else
    if (payload[2] == COMMAND_CLASS_ZIP) {
      the_version = ZIP_VERSION_V5;
    }
#endif

    if (the_version) {
      /*Send ACK first*/
      if (ack_req) {
        SendUDPStatus(ZIP_PACKET_FLAGS0_ACK_RES, cur_flags1, &sl_zwc);
      }

      f.cmdClass = COMMAND_CLASS_VERSION;
      f.cmd = VERSION_COMMAND_CLASS_REPORT;
      f.requestedCommandClass = payload[2];
      f.commandClassVersion = the_version;

      sl_zw_send_zip_data(&sl_zwc, &f, sizeof(f), ClassicZIPNode_CallSendCompleted_cb);
      return TRUE;
    }
  }
  return FALSE;
}

static uint8_t
proxy_supervision_command_handler(zwave_connection_t* c, const uint8_t* payload, uint8_t len, BOOL was_dtls, BOOL ack_req)
{
  (void) len;
  const ZW_SUPERVISION_GET_FRAME *f = (const ZW_SUPERVISION_GET_FRAME *)payload;

  if (f->cmd == SUPERVISION_GET) {
    // TRUE since we are unwrapping the supervision
    if (proxy_command_handler(c, payload + 4, f->encapsulatedCommandLength, was_dtls, ack_req, TRUE)) {
      ZW_SUPERVISION_REPORT_FRAME rep;
      rep.cmdClass = COMMAND_CLASS_SUPERVISION;
      rep.cmd = SUPERVISION_REPORT;
      rep.sessionid = f->sessionid & 0x3f;
      rep.status = SUPERVISION_REPORT_SUCCESS;
      rep.duration = 0;

      sl_zw_send_zip_data(c, &rep, sizeof(rep), ClassicZIPNode_CallSendCompleted_cb);
      return TRUE;
    }
  }

  return FALSE;
}

/**
 * Intercept frame before sending to Z-Wave network
 * \return TRUE if the frame is handled already together with callback, FALSE if the frame should just be sent to Z-Wave network
 */
static BOOL
proxy_command_handler(zwave_connection_t* c, const uint8_t* payload, uint8_t len, BOOL was_dtls, BOOL ack_req, uint8_t bSupervisionUnwrapped)
{
  (void) bSupervisionUnwrapped;
  zwave_connection_t tc = *c;
  tc.scheme = SECURITY_SCHEME_UDP;
  switch (payload[0]) {
    case COMMAND_CLASS_IP_ASSOCIATION:
      if (ack_req) {
        sl_sleeptimer_start_timer_ms(&nak_wait_timer, 150, sl_nak_timer_timeout, NULL, 1, 0);
      }
      return handle_ip_association(&tc, payload, zip_payload_len, was_dtls);

    case COMMAND_CLASS_VERSION:
      return handle_version(payload, zip_payload_len, ack_req);
    case COMMAND_CLASS_SUPERVISION:
      return proxy_supervision_command_handler(&tc, payload, len, was_dtls, ack_req);
    default:
      return FALSE;
  }
}

static int sli_zipudp_drop_frame(zwave_connection_t* c, uint8_t flags1)
{
  if (ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES, flags1, c);
  }
  report_send_completed(TRANSMIT_COMPLETE_ERROR);
  return TRUE;
}

static int sli_zipudp_error_frame(zwave_connection_t* c, uint8_t flags1)
{
  (void) c;
  ERR_PRINTF("Option error\n");
  if (ZIP_PKT_BUF->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) {
    SendUDPStatus(ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_OERR, flags1, &sl_zwc);
  }
  report_send_completed(TRANSMIT_COMPLETE_ERROR);
  return TRUE;
}

static int sli_zipudp_association_process(zwave_connection_t lzw, ZW_COMMAND_ZIP_PACKET *zip_ptk, uint8_t flags1)
{
  (void) flags1; // Unused parameter, but needed for function signature
  /* Do not set source node as virtual node if its COMMAND_CLASS_DEVICE_RESET_LOCALLY */
  if (zip_ptk->payload[0] == COMMAND_CLASS_DEVICE_RESET_LOCALLY) {
    is_device_reset_locally = 1;
  } else {
    is_device_reset_locally = 0;
  }
  ip_association_t *ia = NULL;

  /* When proxying a case2 IP Association, keep the virtual node id when forwarding.
   * Without this exception, the outgoing sender field would be changed to one of
   * the temporary association virtual nodeids.
   *
   * Warning: zw.conn struct notion of local and remote have been swapped. The virtual
   * destination node is now stored as remote IP address.
   * The swapping happens in get_udp_conn() as a preparation for the common case
   * of _replying_. Forwarding, as we are doing here, is a special case.  */
  nodeid_t node_id = sl_node_of_ip(&lzw.ripaddr);
  ia = ip_assoc_lookup_by_virtual_node(node_id);
  if (node_id && ia) {
    ASSERT(ia->type == PROXY_IP_ASSOC);
    ts_param_t p;
    ts_set_std(&p, node_id);
    p.tx_flags = ClassicZIPNode_getTXOptions();
    p.dendpoint = ia->resource_endpoint;
    ASSERT(uip_ipaddr_prefixcmp(&lzw.lipaddr, &ia->resource_ip, 128));
    ASSERT(p.dnode);
    if (is_device_reset_locally) {
      p.snode = MyNodeID;
    } else {
      p.snode = ia->virtual_id;
    }
    p.scheme = lzw.scheme;

    if (!ClassicZIPNode_SendDataAppl(&p, (BYTE*) &zip_ptk->payload, zip_payload_len,
                                     forward_to_ip_assoc_proxy_callback, ia)) {
      WRN_PRINTF("ClassicZIPNode_SendDataAppl failed on case2 IP Assoc proxying\n");
    }
    return TRUE;
  }
  return FALSE;
}

static int sli_zipudp_temp_association_process(uint8_t *payload, BOOL secure)
{
  memcpy(classic_txBuf, payload, zip_payload_len);

  /* If we get this far it's time to create a temporary association */

  temp_association_t *ta = temp_assoc_create(secure);
  if (ta) {
    send_using_temp_assoc(ta);

    /* Check if it's firmware update md get or report */
    if ((payload[0] == COMMAND_CLASS_FIRMWARE_UPDATE_MD)
        && ((payload[1] == FIRMWARE_UPDATE_MD_GET_V3) || (payload[1] == FIRMWARE_UPDATE_MD_REPORT_V3))) {
      temp_assoc_register_fw_lock(ta);
    }
  } else {
    /* Malloc or create virtual failed, abort */
    ASSERT(0);
    ERR_PRINTF("Temporary association creation failed");
    send_zip_ack(TRANSMIT_COMPLETE_ERROR);
    report_send_completed(TRANSMIT_COMPLETE_ERROR);
  }
  return TRUE;
}

static int sli_zipudp_class_process(struct uip_udp_conn* c, uint8_t* pktdata, uint16_t len, BOOL secure)
{
  ZW_COMMAND_ZIP_PACKET* zip_ptk = (ZW_COMMAND_ZIP_PACKET*)pktdata;
  BYTE* payload;

  zip_payload_len = len - ZIP_HEADER_SIZE;
  payload = &zip_ptk->payload[0];

  cur_flags0 = zip_ptk->flags0;
  cur_flags1 = zip_ptk->flags1;// secure ? ZIP_PACKET_FLAGS1_SECURE_ORIGIN : 0;
  sl_zwc.conn = *c;
  sl_zwc.seq = zip_ptk->seqNo;
  sl_zwc.lendpoint = zip_ptk->dEndpoint;
  sl_zwc.rendpoint = zip_ptk->sEndpoint;
  sl_zwc.scheme = (zip_ptk->flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN) ? AUTO_SCHEME  : NO_SCHEME;
  LOG_PRINTF("info: d_ed=%d, s_ed=%d, sch=%d\n", sl_zwc.lendpoint, sl_zwc.rendpoint, sl_zwc.scheme);

  /*
   * This is an ACK for a previously sent command
   */
  if ((zip_ptk->flags0 & ZIP_PACKET_FLAGS0_ACK_RES)) {
    DBG_PRINTF("ACK frame\n");
    sl_udp_command_handler(c, pktdata, len, secure);
  }

  /* Parse header extensions */
  if (zip_ptk->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL) {
    uint16_t ext_hdr_size = 0;

    if ( *payload - 1 == 0 || *payload - 1 > zip_payload_len) {
      ERR_PRINTF("BAD extended header\n");
      return sli_zipudp_drop_frame(&sl_zwc, cur_flags1);
    }

    /*
     * ext_hdr_size reports the actual length in case of EXT_HDR_LENGTH and
     * excluding one byte field
     */
    return_codes_zip_ext_hdr_t rc = parse_CC_ZIP_EXT_HDR(&zip_ptk->payload[1], *payload - 1, &sl_zwc, &ext_hdr_size);
    if (rc == DROP_FRAME) {
      return sli_zipudp_drop_frame(&sl_zwc, cur_flags1);
    } else if (rc == OPT_ERROR) {
      return sli_zipudp_error_frame(&sl_zwc, cur_flags1);
    }
    zip_payload_len -= (ext_hdr_size + 1);
    payload += (ext_hdr_size + 1);
  }

  /*
   * If no ZW Command included just drop
   */
  if ( ((zip_ptk->flags1 & ZIP_PACKET_FLAGS1_ZW_CMD_INCL) == 0) || zip_payload_len == 0 ) {
    DBG_PRINTF("No Zwave command included, Dropping\n");
    return sli_zipudp_drop_frame(&sl_zwc, cur_flags1);
  }
  /* If frame is too large, drop */
  if (zip_payload_len > sizeof(classic_txBuf)) {
    ERR_PRINTF("Frame too large\n");
    return sli_zipudp_drop_frame(&sl_zwc, cur_flags1);
  }
  /* Remember for later if it is a reset command. */

  if (sli_zipudp_association_process(sl_zwc, zip_ptk, cur_flags1)) {
    return TRUE;
  }

  // FALSE since we are not doing supervision unwrapping
  if (proxy_command_handler(&sl_zwc, payload, zip_payload_len, secure, zip_ptk->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ, FALSE)) {
    return TRUE;
  }

  if (zip_payload_len > sizeof(classic_txBuf)) {
    return sli_zipudp_drop_frame(&sl_zwc, cur_flags1);
  }

  return sli_zipudp_temp_association_process(payload, secure);
}

/**
 * Parse an incoming UDP packet addressed for a classic ZWave node. The packet must be already
 * decrypted and located in the backup_buf buffer. uip_buf is unsafe because it will be
 * overwritten if we use async requests as a part of the parsing.
 *
 * Input argument data must point to ZIP hdr and length must count only zip hdr + zip payload
 * Furthermore, callers must ensure that data points to somewhere in the backup_buf buffer.
 */
/* Overview:
 * if bridge not ready: return FALSE
 * if ZIP packet:
 *    setup some globals from the packet
 *    if packet is ACK: sl_udp_command_handler()
 *    syntax check packet, if wrong, go to drop or opt_error
 *    remember device_reset_locally
 *    if (some other module in gateway is busy): (shouldn't this test be earlier?)
 *       requeue with TRANSMIT_COMPLETE_REQUEUE_DECRYPTED, return TRUE
 *    if we can find an association from the destination:
 *       send with sl_zw_send_data_appl(), return TRUE
 *    if (proxy_command_handler()): return TRUE
 *    if (ip_assoc_create() != OK):
 *       reset ip association state and call completedFunc(ERROR), return TRUE
 * else:
 *    reset ip assoc state and call completedFunc(ERROR), return TRUE
 * drop:
 *    send ZIP nack if needed
 *    reset ip assoc state and call completedFunc(ERROR), return TRUE
 * opt_error:
 *    send ZIP error
 *    reset ip assoc state and call completedFunc(ERROR), return TRUE
 */
int ClassicZIPUDP_input(struct uip_udp_conn* c, uint8_t* pktdata, uint16_t len, BOOL secure)
{
  ZW_COMMAND_ZIP_PACKET* zip_ptk = (ZW_COMMAND_ZIP_PACKET*)pktdata;

  DBG_PRINTF("ClassicZIPUDP_input len: %d secure: %d \n", len, secure);

  if (zip_ptk->cmdClass == COMMAND_CLASS_ZIP
      && zip_ptk->cmd == COMMAND_ZIP_PACKET) {
    sli_zipudp_class_process(c, pktdata, len, secure);
  } /* if COMMAND_CLASS_ZIP */
  else {
    DBG_PRINTF("Dropping packet received on Z-Wave port: Not ZIP encapped\n");
    if ((pktdata[0] == 0x16) && (pktdata[1] == 0xfe) && (pktdata[2] == 0xff)) {
      WRN_PRINTF("Dropped packet is DTLS handhshake from port: %d, %d ip:", UIP_HTONS(c->rport), secure);
      uip_debug_ipaddr_print(&c->ripaddr);
    }
    /* unknown packet, drop and continue */
    report_send_completed(TRANSMIT_COMPLETE_ERROR);
  }

  return TRUE;
}

int ClassicZIPNode_dec_input(void * pktdata, int len)
{
  DBG_PRINTF("Decrypted pkt: len: %d ", len);
  sl_print_hex_buf(pktdata, len);
  DBG_PRINTF("\n");

  return 0;
}

/* This function is used in both directions.
   Normally from LAN to PAN
   And used from PAN to LAN after LogicalRewriteAndSend()
 */
int
ClassicZIPNode_input(nodeid_t node, VOID_CALLBACKFUNC(completedFunc) (BYTE, BYTE*, uint16_t),
                     int bFromMailbox, int bRequeued)
{
  (void) bFromMailbox;
  (void) bRequeued;
  /* Overview:
   *   if unknown destination: drop
   *   if not idle: send back to original queue
   *   if from PAN:
   *      call LogicalRewriteAndSend() to find association and forward,
   *      call completedFunc with OK, whether this works or not.
   *   switch on IP protocol:
   *      if UPD:
   *         if (destined to zwave/DTLS port):
   *            return ClassicZIPUDP_input(blabla, completedFunc)
   *         else
   *            throw Port Unreachable
   *            call completedFunc(OK)
   *      if ICMP:
   *         if ping:
   *            send NOP on PAN, if this fails, drop
   *         else:
   *            ignore it and call completedFunc(OK)
   *   drop:
   *      call completedFunc(ERROR)
   */

  nodeid_t nid = node;
  uint16_t dst_port = sl_uip_buf_dst_port();
  uint32_t proto = UIP_PROTO_UDP;
  if (nodemask_nodeid_is_invalid(nid)) {
    ERR_PRINTF("Dropping as the node id: %d is out of range\n", nid);
    completedFunc(TRANSMIT_COMPLETE_ERROR, NULL, 0);
    return TRUE;
  }
  cbCompletedFunc = completedFunc;
  /*Create a backup of package, in order to make async requests. */
  backup_len = sl_backup_zip_len();
  ASSERT(nid);

  /* Destination address of frame has MANGLE_MAGIC, ie, this was a Z-wave frame. */
  if (LogicalRewriteAndSend()) {
    ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
    return TRUE;
  }

  switch (proto) {
    case UIP_PROTO_UDP:
    {
      if (dst_port == UIP_HTONS(ZWAVE_PORT)) {
        struct uip_udp_conn* c = &sl_uip_buf_get_conn();
        return ClassicZIPUDP_input(c, (uint8_t*)ZIP_PKT_BUF,
                                   sl_backup_zip_len(), 0);
      } else { /* Unknown port */
        ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
        return TRUE;
      }
    }
    break;
    default:
      DBG_PRINTF("Unknown protocol %ld\n", proto);
      ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_OK, NULL, NULL);
      return TRUE;
  }
  /* We have processed this packet but it is not delivered. */
  ClassicZIPNode_CallSendCompleted_cb(TRANSMIT_COMPLETE_ERROR, NULL, NULL);
  return TRUE;
}

void
sl_class_zip_node_init()
{
  backup_len = 0;

  ClassicZIPNode_setTXOptions(TRANSMIT_OPTION_ACK
                              | TRANSMIT_OPTION_AUTO_ROUTE
                              | TRANSMIT_OPTION_EXPLORE);
}

void sl_temp_assoc_fw_lock_release_on_timeout(sl_sleeptimer_timer_handle_t *t, void *u);

/**
 * Form a UDP package from the the Z-Wave package. Destination address will
 * be a logical HAN address which contains the source and destination node
 * IDs and endpoints.
 *
 * The resulting destination address is on the form
 * han_pefix::ffaa:0000:[dendpoint][sendpoint]:[dnode][snode]
 */
void
CreateLogicalUDP(ts_param_t* p, unsigned char *pCmd, uint8_t cmdLength)
{
  zwave_connection_t c;

  memset(&c, 0, sizeof(c));

  c.tx_flags = p->tx_flags;
  c.rx_flags = p->rx_flags;
  c.scheme = p->scheme;
  c.rendpoint = p->dendpoint;
  c.lendpoint = p->sendpoint;

  /* Lookup destination node in assoc table*/
  if (p->dendpoint & 0x80) {
    WRN_PRINTF(
      "bit-addressed multichannel encaps should never be sent to virtual nodes\n");
    return;
  }
  temp_association_t *a = temp_assoc_lookup_by_virtual_nodeid(p->dnode);
  if (a) {
    if ((pCmd[0] == COMMAND_CLASS_FIRMWARE_UPDATE_MD)    //If its firmware update md get or report
        && ((pCmd[1] == FIRMWARE_UPDATE_MD_GET_V3) || (pCmd[1] == FIRMWARE_UPDATE_MD_REPORT_V3))) {
      DBG_PRINTF("LogicalRewriteAndSend: Marking temp association %p as locked by firmware update.\n", a);

      /* We could end up reusing temporary association already created for OTA
       * upgrade and this causes OTA to stall so we need to mark it with
       * firmware flag so that its not reused */
      if (temp_assoc_fw_lock.locked_a) {
        DBG_PRINTF("Previous temp association locked by firmware update was %p %d->ANY\n",
                   temp_assoc_fw_lock.locked_a,
                   temp_assoc_fw_lock.locked_a->virtual_id);
      }

      temp_assoc_fw_lock.locked_a = a;
      DBG_PRINTF("New temp association locked by firmware update is %p %d->ANY\n",
                 a,
                 a->virtual_id);

      sl_sleeptimer_start_timer_ms(&temp_assoc_fw_lock.reset_fw_timer, 60000, sl_temp_assoc_fw_lock_release_on_timeout, a, 1, 0);
    }

    c.rport = a->resource_port;
    c.lport = a->was_dtls ? UIP_HTONS(DTLS_PORT) : UIP_HTONS(ZWAVE_PORT);

    /* Association to LAN - use DTLS and association source as source ip */
    sl_ip_of_node(&c.lipaddr, p->snode);
    uip_ipaddr_copy(&c.ripaddr, &a->resource_ip);

    DBG_PRINTF("Packet from nodeid: %d to port: %d IP addr: ", p->snode, UIP_HTONS(c.rport));
    uip_debug_ipaddr_print(&c.ripaddr);
    DBG_PRINTF("data: ")
    sl_print_hex_buf(pCmd, cmdLength);
    DBG_PRINTF("\n");

    sl_zw_send_data_udp(&c, pCmd, cmdLength, NULL, FALSE);
  } else {
    WRN_PRINTF("No temp association found for package\n");
  }
}

static int sli_logic_temp_association_process(temp_association_t *a, nodeid_t han_nodeid)
{
  struct uip_udp_conn c;

  int len = 0;
  if (ZIP_PKT_BUF->flags1 & 0x80) { //extended header
    len = len + ZIP_PKT_BUF->payload[0];
  }

  if ((ZIP_PKT_BUF->payload[len] == COMMAND_CLASS_FIRMWARE_UPDATE_MD)       //If its firmware update md get or report
      && ((ZIP_PKT_BUF->payload[len + 1] == FIRMWARE_UPDATE_MD_GET_V3) || (ZIP_PKT_BUF->payload[len + 1] == FIRMWARE_UPDATE_MD_REPORT_V3))) {
    DBG_PRINTF("LogicalRewriteAndSend: Marking temp association %p as locked by firmware update.\n", a);

    /* We could end up reusing temporary association already created for OTA
     * upgrade and this causes OTA to stall so we need to mark it with
     * firmware flag so that its not reused */
    if (temp_assoc_fw_lock.locked_a) {
      DBG_PRINTF("Previous temp association locked by firmware update was %p %d->ANY\n",
                 temp_assoc_fw_lock.locked_a,
                 temp_assoc_fw_lock.locked_a->virtual_id);
    }

    temp_assoc_fw_lock.locked_a = a;
    DBG_PRINTF("New temp association locked by firmware update is %p %d->ANY\n",
               a,
               a->virtual_id);

    sl_sleeptimer_start_timer_ms(&temp_assoc_fw_lock.reset_fw_timer, 60000, sl_temp_assoc_fw_lock_release_on_timeout, a, 1, 0);
  }
  ZIP_PKT_BUF->dEndpoint = a->resource_endpoint;
  ZIP_PKT_BUF->sEndpoint =  sl_uip_buf_dst_addr().u8[11];
  c.rport = a->resource_port;
  c.lport = a->was_dtls ? UIP_HTONS(DTLS_PORT) : UIP_HTONS(ZWAVE_PORT);

  /* Association to LAN - use DTLS and association source as source ip */
  sl_ip_of_node(&c.sipaddr, han_nodeid);
  uip_ipaddr_copy(&c.ripaddr, &a->resource_ip);
  DBG_PRINTF("Packet from Z-wave side (nodeid: %d) to port: %d IP addr: ", han_nodeid, UIP_HTONS(c.rport));
  uip_debug_ipaddr_print(&c.ripaddr);
  udp_send_wrap(&c, (const uint8_t*) ZIP_PKT_BUF, sl_backup_zip_len(), 0, 0);
  return TRUE;
}

/**
 * Rewrite source and destination addresses of IP package based on
 * logical HAN destination address and temporary association tables.
 */
static int
LogicalRewriteAndSend()
{
  if (sl_uip_buf_dst_addr().u16[4] == MANGLE_MAGIC) {
    nodeid_t han_nodeid = sl_uip_buf_dst_addr().u16[7];
    nodeid_t virtual_nodeid = sl_uip_buf_dst_addr().u16[6];
    DBG_PRINTF("DeMangled HAN node %d Virtual Node %d\n", han_nodeid, virtual_nodeid);

    temp_association_t *a = temp_assoc_lookup_by_virtual_nodeid(virtual_nodeid);
    if (a) {
      sli_logic_temp_association_process(a, han_nodeid);
    } else {
      WRN_PRINTF("No temp association found for package\n");
    }
    return TRUE;
  }
  return FALSE;
}

void
ClassicZIPNode_setTXOptions(uint8_t opt)
{
  txOpts = opt;
}

void
ClassicZIPNode_addTXOptions(uint8_t opt)
{
  txOpts |= opt;
}

uint8_t
ClassicZIPNode_getTXOptions(void)
{
  return txOpts;
}

uint8_t *
ClassicZIPNode_getTXBuf(void)
{
  return classic_txBuf;
}

void
ClassicZIPNode_sendNACK(BOOL __sendNAck)
{
  bSendNAck = __sendNAck;
}

void
ClassicZIPNode_AbortSending()
{
  if (cur_SendDataAppl_handle) {
    ZW_SendDataApplAbort(cur_SendDataAppl_handle);
  }
}
