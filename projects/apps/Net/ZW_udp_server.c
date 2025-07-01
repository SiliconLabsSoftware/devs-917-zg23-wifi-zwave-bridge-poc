/*******************************************************************************
 * @file  ZW_udp_server.c
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
#include "Common/sl_rd_types.h"
#include "Common/sl_common_log.h"
#include "Common/sl_common_config.h"
#include "Common/sl_common_type.h"
#include "Common/sl_uip_def.h"
#include "Common/sl_gw_info.h"

#include "lib/list.h"
#include "lib/memb.h"

#include "Serialapi.h"

#include <ZW_typedefs.h>
#include <ZW_udp_server.h>
#include <ZW_classcmd.h>
#include <ZW_classcmd_ex.h>
#include <ZW_zip_classcmd.h>

#include "lib/random.h"

#include "transport/sl_ts_common.h"
#include "transport/sl_zw_send_data.h"

#include "sl_udp_utils.h"
#include "Net/ZW_udp_server.h"
#include "utls/ipv6_utils.h"

#include "sl_sleeptimer.h"

bool sl_application_cmd_ip_handler(zwave_connection_t *c,
                                   void *pData,
                                   u16_t bDatalen);

/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
#define l3_udp_hdr_len (UIP_IPUDPH_LEN + uip_ext_len)

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
/****************************************************************************/
/*                              PRIVATE FUNCTIONS                           */
/****************************************************************************/

/****************************************************************************/
/*                              EXPORTED FUNCTIONS                          */
/****************************************************************************/

#define DEBUG DEBUG_PRINT

#define UIP_IP_BUF  ((struct uip_ip_hdr *) &uip_buf[UIP_LLH_LEN])
#define UIP_UDP_BUF ((struct uip_udp_hdr *) &uip_buf[uip_l2_l3_hdr_len])

#define MAX_UDP_PAYLOAD_LEN 256

static sl_sleeptimer_timer_handle_t zw_udp_timer;

/**
 * Queue of unsolicited packets from PAN nodes which cannot be delivered until the
 * source has obtained a DHCP IPv4 address.
 */

/** Format of the packets in ipv4_packet_q */
struct ipv4_packet_buffer {
  struct uip_udp_conn c;
  u16_t len;
  uint8_t data[1];
};

/**
 *
 */
struct udp_rx_bk_t {
  list_t next;
  struct uip_udp_conn *c;
};

/**
 * UDP transmit session
 */
struct udp_tx_session {
  list_t next;
  ZW_SendDataAppl_Callback_t cb;
  void *user;
  u8_t seq;
  u8_t src_ep;
  u8_t dst_ep;
  uip_ipaddr_t dst_ip;
  uip_ipaddr_t src_ip;
  u16_t dst_port; //Destination port in network byte order
  sl_sleeptimer_timer_handle_t timeout;
};

MEMB(udp_tx_sessions_memb, struct udp_tx_session, 4);
LIST(udp_tx_sessions_list);

/*
 * Sequence number used in transmissions
 */
static BYTE seqNo = 0;

int zwave_connection_compare(zwave_connection_t *a, zwave_connection_t *b)
{
  return uip_ipaddr_cmp(&a->conn.ripaddr, &b->conn.ripaddr)
         && uip_ipaddr_cmp(&a->conn.sipaddr, &b->conn.sipaddr)
         && (a->conn.lport == b->conn.lport) && (a->conn.rport == b->conn.rport)
         && (a->rendpoint == b->rendpoint) && (a->lendpoint == b->lendpoint);
}

security_scheme_t efi_to_shceme(uint8_t ext1, uint8_t ext2)
{
  switch (ext1) {
    case EFI_SEC_LEVEL_NONE:
      if (ext2 & EFI_FLAG_CRC16) {
        return USE_CRC16;
      } else {
        return NO_SCHEME;
      }
    case EFI_SEC_S0:
      return SECURITY_SCHEME_0;
    case EFI_SEC_S2_UNAUTHENTICATED:
      return SECURITY_SCHEME_2_UNAUTHENTICATED;
    case EFI_SEC_S2_AUTHENTICATED:
      return SECURITY_SCHEME_2_AUTHENTICATED;
    case EFI_SEC_S2_ACCESS:
      return SECURITY_SCHEME_2_ACCESS;
    default:
      return NO_SCHEME;
  }

  WRN_PRINTF("Invalid encapsulation format info\n");
  return NO_SCHEME;
}

/*---------------------------------------------------------------------------*/
/* Max size of ZIP Header incl longest hdr extension we might add*/
/* Extensions currently accounted for: EFI and MULTICAST */
#define MAX_ZIP_HEADER_SIZE (ZIP_HEADER_SIZE + 5)

/* send data over udp connection. */
void sl_zw_send_data_udp(zwave_connection_t *c,
                         const BYTE *dataptr,
                         u16_t datalen,
                         void (*cbFunc)(BYTE, void *),
                         void *user,
                         BOOL ackreq)
{
  static struct uip_udp_conn client_conn;
  security_scheme_t scheme = NO_SCHEME;
  ZW_COMMAND_ZIP_PACKET *pZipPacket;
  char udp_buffer[MAX_UDP_PAYLOAD_LEN + MAX_ZIP_HEADER_SIZE];
  unsigned int i = 0;

  LOG_PRINTF("sl_zw_send_data_udp: %d\n", client_conn.rport);
  sl_print_hex_buf(dataptr, datalen);

  if (datalen + MAX_ZIP_HEADER_SIZE > sizeof(udp_buffer)) {
    ERR_PRINTF("sl_zw_send_data_udp: Package is too large.\n");
    return;
  }

  pZipPacket           = (ZW_COMMAND_ZIP_PACKET *) udp_buffer;
  pZipPacket->cmdClass = COMMAND_CLASS_ZIP;
  pZipPacket->cmd      = COMMAND_ZIP_PACKET;
  pZipPacket->flags0   = ackreq ? ZIP_PACKET_FLAGS0_ACK_REQ : 0;
  if (datalen == 0) {
    /*For UDP mailbox ping */
    DBG_PRINTF("datalen is zero. No command included\n");
    pZipPacket->flags1 = ZIP_PACKET_FLAGS1_HDR_EXT_INCL;
  } else {
    pZipPacket->flags1 =
      ZIP_PACKET_FLAGS1_ZW_CMD_INCL | ZIP_PACKET_FLAGS1_HDR_EXT_INCL;
  }

  pZipPacket->sEndpoint = c->lendpoint;
  pZipPacket->dEndpoint = c->rendpoint;
  pZipPacket->seqNo     = seqNo++;

  if (c->scheme != NO_SCHEME) {
    pZipPacket->flags1 |= ZIP_PACKET_FLAGS1_SECURE_ORIGIN;
  }

  i                        = 0;
  pZipPacket->payload[i++] = 0; /* Filled when header is complete */
  pZipPacket->payload[i++] = 0x80 | ENCAPSULATION_FORMAT_INFO;
  pZipPacket->payload[i++] = 2;
  pZipPacket->payload[i++] = EFI_SEC_LEVEL_NONE;
  pZipPacket->payload[i++] = 0;

  if (c->scheme == SECURITY_SCHEME_UDP) {
    // Reserved for future use.
  } else {
    scheme = c->scheme;
  }

  switch (scheme) {
    case USE_CRC16:
      pZipPacket->payload[4] = EFI_FLAG_CRC16;
      break;
    case SECURITY_SCHEME_0:
      pZipPacket->payload[3] = EFI_SEC_S0;
      break;
    case SECURITY_SCHEME_2_UNAUTHENTICATED:
      pZipPacket->payload[3] = EFI_SEC_S2_UNAUTHENTICATED;
      break;
    case SECURITY_SCHEME_2_AUTHENTICATED:
      pZipPacket->payload[3] = EFI_SEC_S2_AUTHENTICATED;
      break;
    case SECURITY_SCHEME_2_ACCESS:
      pZipPacket->payload[3] = EFI_SEC_S2_ACCESS;
      break;
    case NET_SCHEME:
    case SECURITY_SCHEME_UDP:
    case AUTO_SCHEME:
    case NO_SCHEME:
      break;
  }

  if (((c->rx_flags & RECEIVE_STATUS_TYPE_MASK) == RECEIVE_STATUS_TYPE_MULTI)
      || ((c->rx_flags & RECEIVE_STATUS_TYPE_MASK) == RECEIVE_STATUS_TYPE_BROAD)) {
    pZipPacket->payload[i++] = ZWAVE_MULTICAST_ADDRESSING;
    pZipPacket->payload[i++] = 0; /* This is a zero-length TLV */
  }

  pZipPacket->payload[0] = (BYTE)i;   /* Set header extension length (total of all TLVs) */

  memcpy(udp_buffer + ZIP_HEADER_SIZE + i, dataptr, datalen);

  client_conn = c->conn;

  udp_send_wrap(&client_conn,
                (const uint8_t*)pZipPacket,
                (uint16_t)(ZIP_HEADER_SIZE + datalen + i),
                cbFunc,
                user);
}

void udp_send_wrap(struct uip_udp_conn *c,
                   const void *buf,
                   u16_t len,
                   void (*cbFunc)(BYTE, void *),
                   void *user)
{
  c->ttl = 64;

  /* Do not set the udp timer for DTLS traffic here, its done in DTLS_server.c */
  if (c->lport != UIP_HTONS(DTLS_PORT) && cbFunc) {
    cbFunc(0, user);
  }

#ifdef DISABLE_DTLS
  uip_udp_packet_send(c, buf, len);
#else
  /* Since this is neither portal nor PAN traffic, is must be LAN
   * traffic. Most LAN traffic is dtls encrypted */
  if (c->lport == UIP_HTONS(DTLS_PORT)) {
    DBG_PRINTF("udp_send_wrap() DTLS_PORT. Don't support it\n");
  } else {
    DBG_PRINTF("udp_send_wrap() uip_udp\n");
    sl_udp_packet_send_v6(c, (const uint8_t *) buf, len);
  }
#endif
}

// Wrapper callback to adapt cbFunc to the expected UDP callback signature
static void zw_senddatazip_udp_cb(BYTE status, void *user)
{
  // user
  void (*cbFunc)(BYTE, void *, TX_STATUS_TYPE *) = (void (*)(BYTE, void *, TX_STATUS_TYPE *))user;
  if (cbFunc) {
    cbFunc(status, NULL, NULL);
  }
}

/* send data in network
 * if node, send over z-wave, ip send over udp.
 */
void sl_zw_send_zip_data(zwave_connection_t *c,
                         const void *dataptr,
                         u16_t datalen,
                         void (*cbFunc)(BYTE, void *user, TX_STATUS_TYPE *))
{
  ts_param_t p;
  nodeid_t rnode;

  LOG_PRINTF("sl_zw_send_zip_data: %d, %d bytes\n", c->rport, datalen);

  rnode = sl_node_of_ip(&c->ripaddr);

  /* if c is node, we send data over z-wave network */
  if (rnode) {
    nodeid_t lnode = sl_node_of_ip(&c->lipaddr);

    ts_set_std(&p, rnode);
    p.scheme   = c->scheme;
    p.snode    = lnode;
    p.tx_flags = c->tx_flags;
    p.rx_flags = c->rx_flags;

    p.dendpoint = c->rendpoint;
    p.sendpoint = c->lendpoint;

    if (!sl_zw_send_data_appl(&p, dataptr, datalen, cbFunc, 0) && cbFunc != NULL) {
      cbFunc(TRANSMIT_COMPLETE_FAIL, 0, NULL);
    }
  } else {
    sl_zw_send_data_udp(c,
                        dataptr,
                        datalen,
                        zw_senddatazip_udp_cb,
                        (void *)cbFunc,
                        FALSE);
  }
}

static void ack_session_timeout(sl_sleeptimer_timer_handle_t *t, void *data)
{
  (void) t;
  struct udp_tx_session *s = (struct udp_tx_session *) data;

  if (s->cb) {
    s->cb(TRANSMIT_COMPLETE_FAIL, s->user, NULL);
  }
  list_remove(udp_tx_sessions_list, s);
  memb_free(&udp_tx_sessions_memb, s);
}

/* This will find a member of udp_tx_sessions_list which has uip_udp_c = c and will start timer for that udp_tx_sessions_list */
static void cb_udp_send_done(BYTE b, void *user)
{
  (void) b;
  struct udp_tx_session *s = user;
  if (s) {
    //Set the same udp session timer to half a second now as we got the callback
    sl_sleeptimer_start_timer_ms(&s->timeout,
                                 CLOCK_SECOND / 2,
                                 ack_session_timeout,
                                 s,
                                 1,
                                 0);
  }
}

void sl_zw_send_data_zip_ack(zwave_connection_t *c,
                             const void *dataptr,
                             u8_t datalen,
                             void (*cbFunc)(u8_t, void *, TX_STATUS_TYPE *))
{
  DBG_PRINTF("sl_zw_send_data_zip_ack() \n");
  if (ZW_IsZWAddr(&c->ripaddr)) {
    ts_param_t p;
    ts_set_std(&p, c->ripaddr.u8[0]);
    p.scheme = c->scheme;
    if (!sl_zw_send_data_appl(&p, dataptr, datalen & 0xFF, cbFunc, 0)
        && cbFunc != NULL) {
      cbFunc(TRANSMIT_COMPLETE_FAIL, 0, NULL);
    }
  } else {
    struct udp_tx_session *s;
    s = memb_alloc(&udp_tx_sessions_memb);
    if (s) {
      DBG_PRINTF("Allocated UDP session slot: %d\n",
                 memb_slot_number(&udp_tx_sessions_memb, s));
      s->seq = seqNo;
      uip_ipaddr_copy(&s->dst_ip, &c->ripaddr);
      uip_ipaddr_copy(&s->src_ip, &c->lipaddr);
      s->dst_port = c->rport;
      s->cb       = cbFunc;
      s->src_ep   = c->lendpoint;
      s->dst_ep   = c->rendpoint;
      s->user     = 0;
      list_add(udp_tx_sessions_list, s);
      // Start the udp session timer here to cover the missing callback cb_udp_send_done()
      sl_sleeptimer_start_timer_ms(&s->timeout,
                                   CLOCK_SECOND,
                                   ack_session_timeout,
                                   s,
                                   1,
                                   0);
      sl_zw_send_data_udp(c, dataptr, datalen, cb_udp_send_done, (void *) s, TRUE);
    } else {
      WRN_PRINTF("No more free tx_sessions");
      if (cbFunc) {
        cbFunc(TRANSMIT_COMPLETE_FAIL, 0, NULL);
      }
    }
  }
}

static void do_app_handler(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) u;
  (void) t;
  zw_msg_t msg;

  while (sl_udp_store_zw_msg_get(&msg, 10) == SL_STATUS_OK) {
    /* Send ACK before handling the packet if needed */
    if (msg.ack_req) {
      send_udp_ack(&msg.conn, RES_ACK);
    }
    sl_application_cmd_ip_handler(&msg.conn, msg.data, msg.data_len);
  }
}

void send_udp_ack(zwave_udp_session_t *s, zwave_udp_response_t res)
{
  if (s == NULL) {
    ERR_PRINTF("send_udp_ack: session pointer is NULL\n");
    return;
  }
  /* Ack package, ready to send, just update the seqNr*/
  ZW_COMMAND_ZIP_PACKET ack = {
    COMMAND_CLASS_ZIP,
    COMMAND_ZIP_PACKET,
    ZIP_PACKET_FLAGS0_ACK_RES,   //Parm1
    0,                           //Parm2
    0,                           //Seq
    0,                           //Endpoint
    0,
    { 0 },
  };

  ack.seqNo     = s->seq;
  ack.dEndpoint = s->rendpoint;
  ack.sEndpoint = s->lendpoint;

  switch (res) {
    case RES_ACK:
      ack.flags0 = ZIP_PACKET_FLAGS0_ACK_RES;
      ack.flags1 = 0;
      break;
    case RES_NAK:
      ack.flags0 = ZIP_PACKET_FLAGS0_NACK_RES;
      ack.flags1 = 0;
      break;
    case RES_WAITNG:
      ack.flags0 = ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_WAIT;
      ack.flags1 = 0;
      break;
    case RES_OPT_ERR:
      ack.flags0 = ZIP_PACKET_FLAGS0_NACK_RES | ZIP_PACKET_FLAGS0_NACK_OERR;
      ack.flags1 = 0;
      break;
  }

  udp_send_wrap(&s->conn, (const uint8_t*)&ack, ZIP_HEADER_SIZE, 0, 0);
}

static void sli_zip_ack_send_res(struct uip_udp_conn *c,
                                 const ZW_COMMAND_ZIP_PACKET *pZipPacket)
{
  for (struct udp_tx_session *s = list_head(udp_tx_sessions_list); s; s = list_item_next(s)) {
    if (uip_ipaddr_cmp(&s->dst_ip, &c->ripaddr)
        && uip_ipaddr_cmp(&s->src_ip, &c->sipaddr) && s->dst_port == c->rport
        && s->seq == pZipPacket->seqNo && s->src_ep == pZipPacket->dEndpoint
        && s->dst_ep == pZipPacket->sEndpoint) {
      sl_sleeptimer_stop_timer(&s->timeout);
      if (s->cb) {
        s->cb(TRANSMIT_COMPLETE_OK, 0, NULL);
      }
      list_remove(udp_tx_sessions_list, s);
      DBG_PRINTF("Freed UDP session slot: %d\n",
                 memb_slot_number(&udp_tx_sessions_memb, s));
      memb_free(&udp_tx_sessions_memb, s);
      break;
    }
  }
}

static int sli_udp_zwave_cmd_process(struct uip_udp_conn *c,
                                     const ZW_COMMAND_ZIP_PACKET *pZipPacket,
                                     const uint8_t *payload,
                                     uint16_t udp_payload_len,
                                     u8_t received_secure,
                                     security_scheme_t scheme)
{
  BOOL isMulticast = false;
  struct async_state *s;
  /* payload here is ZWave command without ZIP header */
  s = (struct async_state *) malloc(sizeof(struct async_state));
  uint16_t pcmd_data_len = udp_payload_len;
  uint8_t pcmd_data[udp_payload_len];
  memcpy(pcmd_data, payload, udp_payload_len);

  s->conn.rendpoint = pZipPacket->sEndpoint;
  s->conn.lendpoint = pZipPacket->dEndpoint;
  s->conn.conn      = *c;
  s->conn.rx_flags  = 0;
  s->conn.seq       = pZipPacket->seqNo;

  /* Save ack req for this Z-Wave packet so that dp_app_handler() can use
   * it */
  s->ack_req = pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ;

  if (scheme == AUTO_SCHEME) {
#ifndef DEBUG_ALLOW_NONSECURE
    s->conn.scheme = (pZipPacket->flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN)
                     && received_secure
                     ? SECURITY_SCHEME_UDP
                     : NO_SCHEME;
#else
    s->conn.scheme = (tmp_flags1 & ZIP_PACKET_FLAGS1_SECURE_ORIGIN)
                     ? SECURITY_SCHEME_UDP
                     : NO_SCHEME;

#endif
  } else {
    s->conn.scheme = scheme;
  }

  if (pZipPacket->flags1 & ZIP_PACKET_FLAGS1_ZW_CMD_INCL) {
    if (isMulticast) {
      /*Multicast frames are handled with a delay*/
      s->ack_req = 0;
      sl_udp_store_zw_msg_put(s, pcmd_data, pcmd_data_len);
      sl_sleeptimer_start_timer_ms(&zw_udp_timer,
                                   random_rand() & 0x1af,
                                   do_app_handler,
                                   0,
                                   1,
                                   0);
      return 0;
    } else {
      LOG_PRINTF("sl_udp_store_zw_msg_put\n");
      sl_udp_store_zw_msg_put(s, pcmd_data, pcmd_data_len);
      do_app_handler(NULL, NULL);
    }
  }
  return 0;
}

static int sli_udp_zip_ext_incl_calc(const ZW_COMMAND_ZIP_PACKET *pZipPacket,
                                     security_scheme_t *scheme,
                                     const BYTE *payload)
{
  const tlv_t *opt = (const tlv_t *) &pZipPacket->payload[1];
  while ((const BYTE *) opt < payload) {
    if (opt->type == (ENCAPSULATION_FORMAT_INFO | 0x80)) {
      if (opt->length < 2) {
        return -1;
      }
      *scheme = efi_to_shceme(opt->value[0], opt->value[1]);
    } else if (opt->type & 0x80) { //Check for critical options
      /*Send option error*/
      return -1;
    }
    opt = (const tlv_t *) ((const BYTE *) opt + (opt->length + 2));
  }

  if ((const BYTE *) opt != payload) {
    return -1;
  }
  return 0;
}

static int sli_upd_zip_process(struct uip_udp_conn *c,
                               const ZW_COMMAND_ZIP_PACKET *pZipPacket,
                               const u8_t *data,
                               u16_t len,
                               u8_t received_secure)
{
  (void) data;

  u16_t udp_payload_len;
  const BYTE *payload;
  security_scheme_t scheme = AUTO_SCHEME;
  int tmp_flags1;
  /*Check if this is an ACK response, check with our tx sessions*/
  if (pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_RES) {
    sli_zip_ack_send_res(c, pZipPacket);
  }

  // length of payload removed ZIP Header.
  udp_payload_len = len - ZIP_HEADER_SIZE;
  LOG_PRINTF("payload len: %d\n", udp_payload_len);

  if (pZipPacket->flags0
      & (ZIP_PACKET_FLAGS0_ACK_RES | ZIP_PACKET_FLAGS0_NACK_WAIT)) {
    return 0;
  }

  payload    = &pZipPacket->payload[0];
  if (*payload == 0 || *payload > udp_payload_len) {
    return -1;
  }

  /*Parse header extensions*/
  tmp_flags1 = pZipPacket->flags1;
  if (tmp_flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL) {
    udp_payload_len -= *payload;
    payload += *payload;

    if (sli_udp_zip_ext_incl_calc(pZipPacket, &scheme, payload) == -1) {
      return -1;
    }
  }

  if (udp_payload_len == 0) {
    return -1;
  }

  return sli_udp_zwave_cmd_process(c,
                                   pZipPacket,
                                   payload,
                                   udp_payload_len,
                                   received_secure,
                                   scheme);
}

static int sli_udp_handle_zip_packet(struct uip_udp_conn *c,
                                     const ZW_COMMAND_ZIP_PACKET *pZipPacket,
                                     const u8_t *data,
                                     u16_t len,
                                     u8_t received_secure)
{
  const u8_t keep_alive_ack[] = { COMMAND_CLASS_ZIP,
                                  COMMAND_ZIP_KEEP_ALIVE,
                                  ZIP_KEEP_ALIVE_ACK_RESPONSE };

  /*Spoof multicast queries to look like they were sent to the link local address */
  if (uip_is_addr_linklocal_allrouters_mcast(&c->sipaddr)) {
    WRN_PRINTF("\nuip_is_addr_linklocal_allrouters_mcast doesn't support\n");
    return 0;
  }

  switch (pZipPacket->cmd) {
    case COMMAND_ZIP_KEEP_ALIVE:
      if (pZipPacket->flags0 & ZIP_KEEP_ALIVE_ACK_REQUEST) {
        DBG_PRINTF("Sending keep alive ACK from Gateway to port:%d",
                   UIP_HTONS(c->rport));
        udp_send_wrap(c, (const uint8_t*)&keep_alive_ack, sizeof(keep_alive_ack), 0, 0);
        return 0;
      }
      break;

    case COMMAND_ZIP_PACKET:
    {
      return sli_upd_zip_process(c, pZipPacket, data, len, received_secure);
    }
    default:
      LOG_PRINTF("Invalid Z-Wave package\n");
      break;
  }
  return 0;
}

/* handle a zip packet from a udp connection */
void sl_udp_command_handler(struct uip_udp_conn *c,
                            const u8_t *data,
                            u16_t len,
                            u8_t received_secure)
{
  LOG_PRINTF("=====> sl_udp_command_handler len %d | received_secure %d\n",
             len,
             received_secure);
  const ZW_COMMAND_ZIP_PACKET *pZipPacket =
    (const ZW_COMMAND_ZIP_PACKET *) data;

  DBG_PRINTF("\nPacket from IP: ")
  uip_debug_ipaddr_print(&c->ripaddr);
  DBG_PRINTF("\n");

  if (pZipPacket->cmdClass == COMMAND_CLASS_ZIP_ND) {
    WRN_PRINTF("\nZIP ND doesn't support\n");
    return;
  }

  if (pZipPacket->cmdClass == COMMAND_CLASS_ZIP) {
    sli_udp_handle_zip_packet(c, pZipPacket, data, len, received_secure);
    return;
  }

  if (pZipPacket->flags0 & ZIP_PACKET_FLAGS0_ACK_REQ) {
    zwave_udp_session_t ses;
    ses.seq       = pZipPacket->seqNo;
    ses.conn      = *c;
    ses.lendpoint = pZipPacket->dEndpoint;
    ses.rendpoint = pZipPacket->sEndpoint;
    send_udp_ack(&ses, RES_OPT_ERR);
    return;
  }

  ERR_PRINTF("Invalid package dropped\n");
}
/*---------------------------------------------------------------------------*/
void sl_zw_udp_handler(struct uip_udp_conn *c,
                       uint8_t *uip_appdata,
                       uint16_t uip_datalen)
{
  LOG_PRINTF("Incoming UDP\n");

  // Call ApplicationIPCommandHandler() here
  sl_udp_command_handler(c, uip_appdata, uip_datalen, false);
}

void sl_zw_udp_init(void)
{
  memb_init(&udp_tx_sessions_memb);
  list_init(udp_tx_sessions_list);
  sl_udp_store_zw_msg_init();
}
