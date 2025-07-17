/***************************************************************************/ /**
 * @file sl_zw_send_request.c
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

#include "lib/list.h"
#include "lib/memb.h"

#include "sl_common_log.h"
#include "sl_common_type.h"

#include "Z-Wave/include/ZW_classcmd.h"
#include "Z-Wave/include/ZW_transport_api.h"

#include "sl_ts_common.h"

#include "sl_zw_send_data.h"
#include "sl_zw_send_request.h"

#define NUM_REQS                     4
#define NOT_SENDING                  0xFF
#define ROUTING_RETRANSMISSION_DELAY 250

typedef enum {
  REQ_SENDING,
  REQ_WAITING,
  REQ_DONE,
} req_state_t;

typedef struct send_request_state {
  list_t *list;
  ts_param_t param;
  req_state_t state;
  uint8_t class;
  uint8_t cmd;
  uint16_t timeout;
  void *user;
  ZW_SendRequst_Callback_t callback;
  sl_sleeptimer_timer_handle_t timer;
  clock_time_t round_trip_start;
} send_request_state_t;

MEMB(reqs, struct send_request_state, NUM_REQS);
LIST(reqs_list);

static void sli_send_req_timeout_cb(sl_sleeptimer_timer_handle_t *c, void *d)
{
  (void) c;
  send_request_state_t *s = (send_request_state_t *) d;
  s->param.dnode          = 0;

  s->state = REQ_DONE;
  sl_sleeptimer_stop_timer(&s->timer);
  list_remove(reqs_list, s);
  memb_free(&reqs, s);

  printf("SendRequest timeout waiting for 0x%2x 0x%2x\n", s->class, s->cmd);
  if (s->callback) {
    s->callback(TRANSMIT_COMPLETE_FAIL, 0, 0, 0, s->user);
  }
}

static void sli_send_request_cb(uint8_t status, void *user, TX_STATUS_TYPE *t)
{
  (void)t; // Mark unused parameter
  send_request_state_t *s     = (send_request_state_t *) user;
  clock_time_t round_trip_end = clock_time();
  clock_time_t round_trip_duration;

  if (s->round_trip_start < round_trip_end) {
    round_trip_duration = round_trip_end - s->round_trip_start;
  } else {
    round_trip_duration = 10;
  }

  /*250 ms added to be safe for delays with routing and hops and retransmission */
  round_trip_duration += ROUTING_RETRANSMISSION_DELAY;

  //DBG_PRINTF("round_trip_duration : %lu\n", round_trip_duration);
  if ((status == TRANSMIT_COMPLETE_OK) && (s->state == REQ_SENDING)) {
    s->state = REQ_WAITING;
    sl_sleeptimer_start_timer_ms(&s->timer,
                                 (s->timeout * 10) + round_trip_duration,
                                 sli_send_req_timeout_cb,
                                 s,
                                 1,
                                 0);
  } else {
    sli_send_req_timeout_cb(&s->timer, s);
  }
}

/**
 * Send a request to a node, and trigger the callback once the
 * response is received
 *
 * NOTE! If there is a destination endpoint, then there must be room for 4 uint8_ts before pData
 *
 */
uint8_t sl_zw_send_request(ts_param_t *p,
                           uint8_t *pData,
                           uint8_t dataLength,
                           uint8_t responseCmd,
                           uint16_t timeout,
                           void *user,
                           ZW_SendRequst_Callback_t callback)
{
  send_request_state_t *s;

  if (!callback) {
    goto fail;
  }

  s = memb_alloc(&reqs);

  if (!s) {
    goto fail;
  }

  list_add(reqs_list, s);

  ts_param_make_reply(&s->param,
                      p); //Save the node/endpoint which is supposed to reply

  s->state    = REQ_SENDING;
  s->class    = pData[0];
  s->cmd      = responseCmd;
  s->user     = user;
  s->callback = callback;
  s->timeout  = timeout;

  s->round_trip_start = clock_time();
  if (sl_zw_send_data_appl(p, pData, dataLength, sli_send_request_cb, s)) {
    return TRUE;
  } else {
    list_remove(reqs_list, s);
    memb_free(&reqs, s);
  }

  fail:
  ERR_PRINTF("sl_zw_send_request fail\n");
  return FALSE;
}

void ZW_Abort_SendRequest(uint8_t n)
{
  send_request_state_t *s;
  for (s = list_head(reqs_list); s; s = list_item_next(s)) {
    if (s->state == REQ_WAITING && (s->param.snode == n)) {
      s->state = REQ_DONE;
      sl_sleeptimer_stop_timer(&s->timer);
      list_remove(reqs_list, s);
      memb_free(&reqs, s);
      s->callback(TRANSMIT_COMPLETE_FAIL, 0, 0, 0, s->user);
    }
  }
}

BOOL sl_send_request_appl_cmd_handler(ts_param_t *p,
                                      ZW_APPLICATION_TX_BUFFER *pCmd,
                                      uint16_t cmdLength)
{
  send_request_state_t *s;
  for (s = list_head(reqs_list); s; s = list_item_next(s)) {
    if (s->state == REQ_WAITING && ts_param_cmp(&s->param, p)
        && s->class == pCmd->ZW_Common.cmdClass && s->cmd == pCmd->ZW_Common.cmd) {
      /*Only accept the command if it was received with the same security scheme which was used
       * during transmission */
      if (s->param.scheme != p->scheme) {
        WRN_PRINTF("Message with wrong scheme received was %i expected %i\n",
                   p->scheme,
                   s->param.scheme);
        continue;
      }

      s->state = REQ_DONE;

      sl_sleeptimer_stop_timer(&s->timer);
      list_remove(reqs_list, s);

      if (s->callback(TRANSMIT_COMPLETE_OK,
                      p->rx_flags,
                      pCmd,
                      cmdLength,
                      s->user)) {
        WRN_PRINTF("sl_zw_send_request Callback returned 1. There are more reports"
                   " expected.\n");
        s->state = REQ_WAITING;
        sl_sleeptimer_start_timer_ms(&s->timer,
                                     (s->timeout * 10) + ROUTING_RETRANSMISSION_DELAY,
                                     sli_send_req_timeout_cb,
                                     s,
                                     1,
                                     0);
        list_add(reqs_list, s);
      } else {
        memb_free(&reqs, s);
      }
      return TRUE;
    }
  }
  return FALSE;
}

void sl_zw_send_request_init()
{
  memb_init(&reqs);
  list_init(reqs_list);
}
