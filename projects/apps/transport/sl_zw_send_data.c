/***************************************************************************/ /**
 * @file sl_zw_send_data.c
 * @brief USART driver function
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
#include "sl_common_log.h"
#include "sl_common_config.h"
#include "sl_rd_types.h"
#include "sl_gw_info.h"

#include "Serialapi.h"

#include "lib/list.h"
#include "lib/memb.h"

#include "utls/sl_zw_validator.h"
#include "utls/sl_node_sec_flags.h"
#include "utls/zgw_crc.h"

#include "sl_zw_frm.h"
#include "sl_ts_param.h"
#include "sl_security_scheme0.h"

#include "ZW_classcmd.h"
#include "ZW_classcmd_ex.h"

#include "Z-Wave/include/ZW_transport_api.h"

#include "sl_ts_common.h"

#include "sl_sleeptimer.h"
#include "sl_status.h"

#define MAX_BUF_ENDPOINT_DATA 512

/*
 * SendData Hierarchy, each, level wraps the previous. A higher level call MUST only call lower level calls.
 *
 * SendRequest          - available from application space, send a command and wait for a reply
 * SendDataAppl         - available from application space, send a command and put it in a queue
 * SendEndpoint         - internal call, single session call add endpoint header
 * SendSecurity         - internal call, single session call add security header
 * SendTransportService - internal call, single session call do transport service fragmentation
 * SendData             - internal call, single session send the data
 */

typedef struct {
  void *next;
  zw_frame_buffer_element_t *fb;
  void *user;
  ZW_SendDataAppl_Callback_t callback;
  sl_sleeptimer_timer_handle_t
    discard_timer;   /* Timer for discarding element if it stays in the queue too long */
  uint8_t
    reset_span;   /* This flag will be set on sending activation set to the end node. On successful ack
                         S2 Span for the destination node will be reset */
} send_data_appl_session_t;

static uint8_t lock = 0;
static sl_sleeptimer_timer_handle_t emergency_timer;
static sl_sleeptimer_timer_handle_t backoff_timer;

static nodeid_t backoff_node; //Node on which the backoff timer is started

LIST(session_list);
MEMB(session_memb, send_data_appl_session_t, 8);

static uint8_t lock_ll        = 0;
//static uint8_t resend_counter = 0;
LIST(send_data_list);

static send_data_appl_session_t *sl_appl_cur_session;
static send_data_appl_session_t *sl_data_cur_session;

static void do_discard_timeout_memb(sl_sleeptimer_timer_handle_t *handle,
                                    void *data);
static void backoff_timer_timeout(sl_sleeptimer_timer_handle_t *handle,
                                  void *data);
static void emergency_timer_timeout(sl_sleeptimer_timer_handle_t *handle,
                                    void *data);
sl_status_t zw_send_data_post_event(uint32_t event, void *data);
sl_status_t zw_send_data_get_event(void *msg, uint32_t timeout);
void sl_zw_send_data_queue_init(void);

enum {
  SEND_EVENT_SEND_NEXT,
  SEND_EVENT_SEND_NEXT_LL,
  SEND_EVENT_SEND_NEXT_DELAYED,
  SEND_EVENT_TIMER,
};

static void
sli_zw_appl_send_data_cb_ex(uint8_t status, void *user, TX_STATUS_TYPE *ts)
{
  send_data_appl_session_t *s = (send_data_appl_session_t *) user;
  uint32_t backoff_interval   = 0;
  LOG_PRINTF("sli_zw_appl_send_data_cb_ex\n");

  if (!lock) {
    ERR_PRINTF("Double callback! ");
    return;
  }
  lock = FALSE;

  /*Check if this is a get message, and set the backoff accordingly */
  if ((status == TRANSMIT_COMPLETE_OK) && (ts != NULL)
      && sl_zw_validator_is_cmd_get(s->fb->frame_data[0], s->fb->frame_data[1])) {
    /* Make some room for the report */
    backoff_interval = ts->wTransmitTicks * 10 + 250;
    backoff_node     = s->fb->param.dnode;
    //
    sl_sleeptimer_start_timer_ms(&backoff_timer,
                                 backoff_interval,
                                 backoff_timer_timeout,
                                 (void *) &backoff_node,
                                 1,
                                 0);
  } else {
    backoff_interval = 0;
    // This is an async post, the next element will only be send when contiki has scheduled the event.
    zw_send_data_post_event(SEND_EVENT_SEND_NEXT, NULL);
  }

  if (status == TRANSMIT_COMPLETE_OK) {
    if (s->reset_span) {
      // reset s2, but we don't support this.
      s->reset_span = 0;
    }
  } else {
    WRN_PRINTF("SendDataAppl status failed!\n");
  }

  zw_frame_buffer_free(s->fb);
  memb_free(&session_memb, s);

  // call user callback.
  if (s->callback) {
    s->callback(status, s->user, ts);
  }
}

/**
 * Timeout function that removes an expired session_memb from the send_data_list and memb_frees it.
 */
static void do_discard_timeout_memb(sl_sleeptimer_timer_handle_t *handle,
                                    void *data)
{
  (void)handle; // Mark unused parameter
  send_data_appl_session_t *s = (send_data_appl_session_t *) data;
  char retval;

  if (!s) {
    return;
  }
  list_remove(send_data_list, s);
  // ERR_PRINTF("Discarding %p because maximum delay was exceeded in dest\n", s);
  retval = memb_free(&session_memb, s);
  if (retval != 0) {
    printf("memb_free() failed in %s(). Return code %d, ptr %p\n",
           __func__,
           retval,
           s);
  }
  if (s->callback) {
    s->callback(TRANSMIT_COMPLETE_FAIL, s->user, NULL);
  }
}

const char *lc_status_to_string(int state)
{
  switch (state) {
    case TRANSMIT_COMPLETE_OK: return "TRANSMIT_COMPLETE_OK";
    case TRANSMIT_COMPLETE_NO_ACK: return "TRANSMIT_COMPLETE_NO_ACK";
    case TRANSMIT_COMPLETE_FAIL: return "TRANSMIT_COMPLETE_FAIL";
    case TRANSMIT_ROUTING_NOT_IDLE: return "TRANSMIT_ROUTING_NOT_IDLE";
    case TRANSMIT_COMPLETE_REQUEUE_QUEUED: return "TRANSMIT_COMPLETE_REQUEUE_QUEUED";
    case TRANSMIT_COMPLETE_REQUEUE: return "TRANSMIT_COMPLETE_REQUEUE";
    case TRANSMIT_COMPLETE_ERROR: return "TRANSMIT_COMPLETE_ERROR";
    default:
      return "Unknown";
  }
}
/**
 * Callback function activated when a callback is received from the SendData API functions on the Z-Wave chip.
 *
 *\param status  Transmit status code
 *\param ts      Transmit status report
 */
static void send_data_callback_func(uint8_t status, TX_STATUS_TYPE *ts)
{
  LOG_PRINTF("send_data_callback_func() | status %s\n", lc_status_to_string(status));
  //  enum en_queue_state queue_state = get_queue_state();

  if (!lock_ll) {
    ERR_PRINTF("Double callback?\n");
    return;
  }
  sl_sleeptimer_stop_timer(&emergency_timer);

  send_data_appl_session_t *s = list_pop(send_data_list);
  if (s == NULL) {
    ERR_PRINTF("send_data_appl_session_t OMG null!\n");
    return;
  }

  zw_frame_buffer_free(s->fb);
  memb_free(&session_memb, s);
  if (s) {
    if (s->callback) {
      s->callback(status, s->user, ts);
    }
  } else {
    ASSERT(0);
  }

  // send next.
  int ln = list_length(send_data_list);
  if (ln) {
    LOG_PRINTF("zw_send_data_post_event: SEND_EVENT_SEND_NEXT_LL");
    zw_send_data_post_event(SEND_EVENT_SEND_NEXT_LL, NULL);
    LOG_PRINTF(" done\n");
  }

  lock_ll = FALSE;
}

/**
 * Low level send data. This call will not do any encapsulation except transport service encap
 */
uint8_t send_data(ts_param_t *p,
                  const uint8_t *data,
                  uint16_t len,
                  ZW_SendDataAppl_Callback_t cb,
                  void *user)
{
  LOG_PRINTF("send_data() | len %d\n", len);
  send_data_appl_session_t *s;

  s = memb_alloc(&session_memb);
  if (s == 0) {
    DBG_PRINTF("OMG! No more queue space\n");
    return FALSE;
  }

  s->fb = zw_frame_buffer_create(p, data, len);
  if (s->fb == NULL) {
    memb_free(&session_memb, s);
    DBG_PRINTF("OMG! No more frame buffer space\n");
    return FALSE;
  }
  s->user     = user;
  s->callback = cb;

  list_add(send_data_list, s);

  if (s->fb->param.discard_timeout) {
    DBG_PRINTF("Starting %.0fms discard timer on send_data_list element: %p\n",
               ((float) (s->fb->param.discard_timeout)) / CLOCK_SECOND * 1000,
               s);
    sl_sleeptimer_start_timer_ms(&s->discard_timer,
                                 s->fb->param.discard_timeout,
                                 do_discard_timeout_memb,
                                 s,
                                 1,
                                 0);
  }
  // send next
  LOG_PRINTF("zw_send_data_post_event: SEND_EVENT_SEND_NEXT_LL");
  zw_send_data_post_event(SEND_EVENT_SEND_NEXT_LL, NULL);
  LOG_PRINTF(" done\n");
  return TRUE;
}

/**
 * Send data to an endpoint and do endpoint encap security encap CRC16 or transport service encap
 * if needed. This function is not reentrant. It will only be called from the sl_zw_send_data_appl event tree
 * @param p
 * @param data
 * @param len
 * @param cb
 * @param user
 * @return
 */
uint8_t send_endpoint(ts_param_t *p,
                      const uint8_t *data,
                      uint16_t len,
                      ZW_SendDataAppl_Callback_t cb,
                      void *user)
{
  uint16_t new_len;
  security_scheme_t scheme;
  static uint8_t
    new_buf[MAX_BUF_ENDPOINT_DATA];   //Todo we should have some max frame size

  new_len = len;

  if (len > sizeof(new_buf)) {
    return FALSE;
  }

  if (p->dendpoint || p->sendpoint) {
    new_buf[0] = COMMAND_CLASS_MULTI_CHANNEL_V2;
    new_buf[1] = MULTI_CHANNEL_CMD_ENCAP_V2;
    new_buf[2] = p->sendpoint;
    new_buf[3] = p->dendpoint;
    new_len += 4;

    if (new_len > sizeof(new_buf)) {
      return FALSE;
    }

    memcpy(&new_buf[4], data, len);
  } else {
    memcpy(new_buf, data, len);
  }

  /*Select the right security shceme*/
  scheme = zw_scheme_select(p, data, len);
  LOG_PRINTF("send_endpoint() | Sending with scheme %2X\n", scheme);
  switch (scheme) {
    case USE_CRC16:
      /* CRC16 Encap frame if destination supports it and if its a single fragment frame
       *
       *
       * */
      if (new_len < META_DATA_MAX_DATA_SIZE
          && new_buf[0] != COMMAND_CLASS_TRANSPORT_SERVICE
          && new_buf[0] != COMMAND_CLASS_SECURITY
          && new_buf[0] != COMMAND_CLASS_SECURITY_2
          && new_buf[0] != COMMAND_CLASS_CRC_16_ENCAP) {
        uint16_t crc;
        memmove(new_buf + 2, new_buf, new_len);
        new_buf[0] = COMMAND_CLASS_CRC_16_ENCAP;
        new_buf[1] = CRC_16_ENCAP;
        crc = zgw_crc16(CRC_INIT_VALUE, (uint8_t *) new_buf, new_len + 2);
        new_buf[2 + new_len]     = (crc >> 8) & 0xFF;
        new_buf[2 + new_len + 1] = (crc >> 0) & 0xFF;
        new_len += 4;
      }
      return send_data(p, new_buf, new_len, cb, user);
    case NO_SCHEME:
      if (p->tx_flags & TRANSMIT_OPTION_MULTICAST) {
        WRN_PRINTF("TODO: implement non-secure multicast\n");
        return FALSE;
      } else {
        return send_data(p, new_buf, new_len, cb, user);
      }
      break;
    case SECURITY_SCHEME_0:
      if (p->tx_flags & TRANSMIT_OPTION_MULTICAST) {
        WRN_PRINTF("Attempt to transmit multicast with S0\n");
        return FALSE;
      } else {
        return sec0_send_data(p, new_buf, new_len, cb, user);
      }
      break;
    case SECURITY_SCHEME_2_ACCESS:
    case SECURITY_SCHEME_2_AUTHENTICATED:
    case SECURITY_SCHEME_2_UNAUTHENTICATED:
      p->scheme = scheme;
      WRN_PRINTF("don't support package in S2\n");
      break;
    default:
      break;
  }
  return FALSE;
}

static void send_first()
{
  LOG_PRINTF("send_first at TIME: %ld\n", osKernelGetTickCount());
  sl_appl_cur_session = list_pop(session_list);
  if (!sl_appl_cur_session) {
    return;
  }

  lock   = TRUE;
  int rc = send_endpoint(&sl_appl_cur_session->fb->param,
                         sl_appl_cur_session->fb->frame_data,
                         sl_appl_cur_session->fb->frame_len,
                         sli_zw_appl_send_data_cb_ex,
                         sl_appl_cur_session);

  if (!rc) {
    sli_zw_appl_send_data_cb_ex(TRANSMIT_COMPLETE_ERROR,
                                sl_appl_cur_session,
                                NULL);
  }
}

uint8_t sl_zw_send_data_appl(ts_param_t *p,
                             void *pData,
                             uint16_t dataLength,
                             ZW_SendDataAppl_Callback_t callback,
                             void *user)
{
  send_data_appl_session_t *s;
  uint8_t *c             = (uint8_t *) pData; //for debug message
  const uint8_t lr_nop[] = { COMMAND_CLASS_NO_OPERATION_LR, 0 };

  s = memb_alloc(&session_memb);
  if (s == 0) {
    DBG_PRINTF("OMG! No more queue space\n");
    return 0;
  }
  /* ZGW-3373: SPAN is reset for the node where Firmware activation set frame is
   * sent to prevent S2 from dropping frame because the sequence number of
   * activation report matching to last two frames S2 duplication detection keeps
   * track of. Protocol had an issue(fixed now) of not saving SPAN and sequence number at the
   * right time. This code will take care of nodes without that protocol fix.
   * When the node reboots and sends firmware activation report, GW will try to
   * sync by sending s2 nonce report and only one fw act report frame will be dropped
   */
  if ((c[0] == COMMAND_CLASS_FIRMWARE_UPDATE_MD_V4)
      && (c[1] == FIRMWARE_UPDATE_ACTIVATION_SET_V4)) {
    DBG_PRINTF("Intercepting Firmware activation set command\n");
    s->reset_span = 1;
  }

  if ((c[0] == COMMAND_CLASS_NO_OPERATION) && is_lr_node(p->dnode)) {
    s->fb = zw_frame_buffer_create(p, lr_nop, sizeof(lr_nop));
    DBG_PRINTF("Sending %d->%d, class: 0x%x, cmd: 0x%x, len: %d\n",
               p->snode,
               p->dnode,
               COMMAND_CLASS_NO_OPERATION_LR,
               0,
               2);
  } else {
    s->fb = zw_frame_buffer_create(p, pData, dataLength);
  }

  if (s->fb == NULL) {
    memb_free(&session_memb, s);
    ERR_PRINTF("sl_zw_send_data_appl: malloc failed\r\n");
    return 0;
  }
  s->user     = user;
  s->callback = callback;

  list_add(session_list, s);
  LOG_PRINTF("zw_send_data_post_event: SEND_EVENT_SEND_NEXT TIME %ld\n",
             osKernelGetTickCount());
  // send next
  int ret = zw_send_data_post_event(SEND_EVENT_SEND_NEXT, NULL);
  if (ret != SL_STATUS_OK) {
    ERR_PRINTF("sl_zw_send_data_appl: Failed to post event %d\r\n", ret);
  }
  LOG_PRINTF(" done\n");

  /*return a handle that we may use to abort the transmission */
  return (((void *) s - session_memb.mem) / session_memb.size) + 1;
}

void ZW_SendDataApplAbort(uint8_t handle)
{
  send_data_appl_session_t *s;
  uint8_t h = handle - 1;

  if (handle == 0 || handle > session_memb.num) {
    ERR_PRINTF("ZW_SendDataAppl_Abort: Invalid handle\n");
    return;
  }

  s = (send_data_appl_session_t *) (session_memb.mem + session_memb.size * h);

  if (s == sl_appl_cur_session) {
    /*Transmission is in progress so stop the module from making more routing attempts
     * This will trigger a transmit complete fail or ok for the current transmission. At some
     * point this will call sli_zw_appl_send_data_cb_ex which will free the session */
    ZW_SendDataAbort();

    /*Cancel security timers in case we are waiting for some frame from target node*/
    sec0_abort_all_tx_sessions();

    /* Cancel transport service timer, in case we are waiting for some frame from target node.
     * TODO*/
    //TransportService_SendDataAbort();
  } else {
    if (!list_contains(session_list, s)) {
      /* This is an orphaned callback. Ignore it.*/
      return;
    }

    /*Remove the session from the list */
    list_remove(session_list, s);

    /*De-allocate the session */
    memb_free(&session_memb, s);
    if (s->callback) {
      s->callback(TRANSMIT_COMPLETE_FAIL, s->user, NULL);
    }
  }
}

/* The component is idle if both the application level queue and the
 * low level queue are empty and there are no current sessions from
 * either queue.
 */
bool ZW_SendDataAppl_idle(void)
{
  return ((sl_appl_cur_session == NULL) && (sl_data_cur_session == NULL)
          && (list_head(session_list) == NULL)
          && (list_head(send_data_list) == NULL));
}

void sl_zw_send_data_appl_init()
{
  lock    = FALSE;
  lock_ll = FALSE;
  sec0_abort_all_tx_sessions();
  list_init(session_list);
  list_init(send_data_list);
  memb_init(&session_memb);

  // init thread.
  sl_zw_send_data_queue_init();
}

/**
 * Calculate a priority for the scheme
 */
static int scheme_prio(security_scheme_t a)
{
  switch (a) {
    case SECURITY_SCHEME_UDP:
      return 0xFFFF;
    case SECURITY_SCHEME_2_ACCESS:
      return 4;
    case SECURITY_SCHEME_2_AUTHENTICATED:
      return 3;
    case SECURITY_SCHEME_2_UNAUTHENTICATED:
      return 2;
    case SECURITY_SCHEME_0:
      return 1;
    default:
      return 0;
  }
}

static void emergency_timer_timeout(sl_sleeptimer_timer_handle_t *handle,
                                    void *data)
{
  (void)handle; // Mark unused parameter
  (void)data;   // Mark unused parameter
  printf("Missed serialAPI callback!\n");
  send_data_callback_func(TRANSMIT_COMPLETE_FAIL, 0);
}

static void backoff_timer_timeout(sl_sleeptimer_timer_handle_t *handle,
                                  void *data)
{
  (void)handle; // Mark unused parameter
  (void)data;   // Mark unused parameter
  printf("Backoff timer expired\n");
  zw_send_data_post_event(SEND_EVENT_SEND_NEXT, NULL);
  LOG_PRINTF(" done\n");
}

/**
 * return true if a has a larger or equal scheme to b
 */
int scheme_compare(security_scheme_t a, security_scheme_t b)
{
  return scheme_prio(a) >= scheme_prio(b);
}

void sl_zw_send_data_appl_rx_notify(const ts_param_t *c,
                                    const uint8_t *frame,
                                    uint16_t length)
{
  (void)frame;  // Mark unused parameter
  (void)length; // Mark unused parameter
  bool running;
  sl_sleeptimer_is_timer_running(&backoff_timer, &running);
  if (running && (c->snode == backoff_node)) {
    //ERR_PRINTF("Backoff timer stopped\n");
    /*Stop the backoff timer and send the next message */
    sl_sleeptimer_stop_timer(&backoff_timer);
    // send next session.
    zw_send_data_post_event(SEND_EVENT_SEND_NEXT, NULL);
  }
}

/*============================================================================
** os api
*/
static osMessageQueueId_t sli_zw_event_queue;

typedef int (*zw_event_handler_cb_t)(uint32_t ev, void *data);

typedef struct {
  uint32_t ev;
  zw_event_handler_cb_t cb;
} zw_tb_event_t;

/*===========================================================================*/
/**
 * @brief .
 *
 * @param[in] none
 * @return void
 * @note
 */
sl_status_t zw_send_data_post_event(uint32_t event, void *data)
{
  sl_cc_net_ev_t msg = { .ev = event, .ev_data = data };

  return osMessageQueuePut(sli_zw_event_queue, (void *) &msg, 0, osWaitForever);;
}

/*===========================================================================*/
/**
 * @brief .
 *
 * @param[in] none
 * @return void
 * @note
 */
sl_status_t zw_send_data_get_event(void *msg, uint32_t timeout)
{
  if (osMessageQueueGet(sli_zw_event_queue, msg, NULL, timeout) == osOK) {
    return SL_STATUS_OK;
  }
  return SL_STATUS_TIMEOUT;
}

static int zw_send_data_timer_handler(uint32_t ev, void *data);
static int zw_send_data_start_handler(uint32_t ev, void *data);
static int zw_send_data_next_handler(uint32_t ev, void *data);

const zw_tb_event_t zw_send_data_event_tb[] = {
  { SEND_EVENT_TIMER, zw_send_data_timer_handler },
  { SEND_EVENT_SEND_NEXT, zw_send_data_start_handler },
  { SEND_EVENT_SEND_NEXT_LL, zw_send_data_next_handler },
};

#define SL_ZW_SEND_DATA_EVT_LENGHT \
  sizeof(zw_send_data_event_tb) / sizeof(zw_send_data_event_tb[0])

static int zw_send_data_timer_handler(uint32_t ev, void *data)
{
  (void)ev;   // Mark unused parameter
  (void)data; // Mark unused parameter
  if (data == (void *) &emergency_timer) {
    return 0;
  }
  if (data == (void *) &backoff_timer) {
    return 0;
  }
  return -1;
}

static int zw_send_data_start_handler(uint32_t ev, void *data)
{
  (void)ev;   // Mark unused parameter
  (void)data; // Mark unused parameter
  LOG_PRINTF("event get: SEND_EVENT_SEND_NEXT\n");
  if (lock) {
    ERR_PRINTF("zw_send_data_start_handler: lock is set\n");
    return -1;
  }
  send_first();
  return 0;
}

static int zw_send_data_next_handler(uint32_t ev, void *data)
{
  (void)ev;   // Mark unused parameter
  (void)data; // Mark unused parameter
  uint8_t rc          = 0;
  LOG_PRINTF("event get: SEND_EVENT_SEND_NEXT_LL\n");

  sl_data_cur_session = list_head(send_data_list);

  if (sl_data_cur_session == NULL) {
    return false;
  }

  LOG_PRINTF("Sending %d->%d, ",
             sl_data_cur_session->fb->param.snode,
             sl_data_cur_session->fb->param.dnode);
  // sl_print_hex_buf(sl_data_cur_session->fb->frame_data,
  //                  sl_data_cur_session->fb->frame_len);
  LOG_PRINTF("\n");

  if (sl_data_cur_session && lock_ll == FALSE) {
    if (sl_data_cur_session->fb->param.discard_timeout) {
      //Prevent a timer to discard the frame if the session has a discard_timeout defined.
      DBG_PRINTF("SEND_EVENT_SEND_NEXT_LL | Stopping discard timer of "
                 "send_data_list element: %p\n",
                 sl_data_cur_session);
      sl_sleeptimer_stop_timer(&sl_data_cur_session->discard_timer);
    }

    lock_ll = TRUE;
    rc      = FALSE;

    if (sl_data_cur_session->fb->frame_len >= META_DATA_MAX_DATA_SIZE) {
      WRN_PRINTF("don't support transport service!\n");
    } else {
      if (sl_data_cur_session->fb->param.snode != MyNodeID) {
        DBG_PRINTF("SEND_EVENT_SEND_NEXT_LL | ZW_SendData_Bridge \n");

        rc = ZW_SendData_Bridge(sl_data_cur_session->fb->param.snode,
                                sl_data_cur_session->fb->param.dnode,
                                (uint8_t *) sl_data_cur_session->fb->frame_data,
                                sl_data_cur_session->fb->frame_len,
                                sl_data_cur_session->fb->param.tx_flags,
                                send_data_callback_func);
      } else {
        DBG_PRINTF("SEND_EVENT_SEND_NEXT_LL | else \n");
        sl_data_cur_session->fb->param.snode = 0x00ff;
        rc = ZW_SEND_DATA(sl_data_cur_session->fb->param.dnode,
                          (uint8_t *) sl_data_cur_session->fb->frame_data,
                          sl_data_cur_session->fb->frame_len,
                          sl_data_cur_session->fb->param.tx_flags,
                          send_data_callback_func);
      }
    }

    if (!rc) {
      ERR_PRINTF("ZW_SendData(_Bridge) returned FALSE\n");
      send_data_callback_func(TRANSMIT_COMPLETE_FAIL, 0);
    } else {
      sl_sleeptimer_start_timer_ms(&emergency_timer,
                                   65 * 1000UL,
                                   emergency_timer_timeout,
                                   NULL,
                                   1,
                                   0);
    }
  }
  return rc;
}

int32_t zw_send_data_event_process(uint32_t ev, void *data)
{
  (void)data; // Mark unused parameter
  for (unsigned int i = 0; i < SL_ZW_SEND_DATA_EVT_LENGHT; i++) { // Cast to int for signed comparison
    if (ev == zw_send_data_event_tb[i].ev) {
      if (zw_send_data_event_tb[i].cb) {
        zw_send_data_event_tb[i].cb(ev, data);
      }
      break;
    }
  }
  return 0;
}

void sl_zw_send_data_queue_init(void)
{
  sli_zw_event_queue = osMessageQueueNew(3, sizeof(sl_cc_net_ev_t), NULL);
}

void sl_zw_layer_data_process(void)
{
  sl_cc_net_ev_t msg;
  if (zw_send_data_get_event(&msg, 1) == osOK) {
    // process event;
    zw_send_data_event_process(msg.ev, msg.ev_data);
    if (msg.ev_data) {
      free(msg.ev_data);
    }
  }
}
