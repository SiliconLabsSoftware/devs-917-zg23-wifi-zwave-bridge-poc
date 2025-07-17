/***************************************************************************/ /**
 * @file CC_inclusionController.c
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
#include "utls/ipv6_utils.h" /* sl_node_of_ip */
#include "utls/sl_node_sec_flags.h"
#include "Common/sl_gw_info.h" /* MyNodeID */
#include "Common/sl_common_log.h"
#include "Common/sl_common_type.h"
#include "Serialapi.h"
#include "ZW_classcmd_ex.h"
#include "transport/sl_ts_common.h"
#include "transport/sl_zw_send_data.h"
#include "transport/sl_zw_send_request.h"
#include "threads/sl_security_layer.h"
#include "ip_translate/sl_zw_resource.h"
#include "Net/ZW_udp_server.h"
#include "CC_InclusionController.h"
#include "CC_NetworkManagement.h"

#include "sl_sleeptimer.h"

typedef enum {
  EV_REQUEST_SUC_INCLUSION,
  EV_SUC_STARTED,
  EV_INITIATE,
  EV_COMPLETE,
  EV_TX_DONE_OK,
  EV_TX_DONE_FAIL,
  EV_TIMEOUT,
  EV_ADD_NODE_DONE,
  EV_S0_INCLUSION,
  EV_S0_INCLUSION_DONE
} handler_events_t;

static struct {
  enum {
    STATE_IDLE,
    STATE_WAIT_FOR_SUC_ACCEPT,
    STATE_WAIT_FOR_ADD,
    STATE_WAIT_S0_INCLUSION,
    STATE_PERFORM_S0_INCLUSION
  } state;
  sl_sleeptimer_timer_handle_t timer;
  inclusion_controller_cb_t complete_func;

  nodeid_t node_id;
  union {
    nodeid_t inclusion_conrtoller;
    nodeid_t suc_node;
  };
  uint8_t is_replace;
  zwave_connection_t connection;
} handler_state;

static void handler_fsm(handler_events_t ev, void *data);

/**
 * @brief Get the string name of a handler event.
 *
 * @param ev Handler event.
 * @return const char* Name of the event.
 */
const char *handler_event_name(handler_events_t ev)
{
  static char str[25];

  switch (ev) {
    case EV_REQUEST_SUC_INCLUSION:
      return "EV_REQUEST_SUC_INCLUSION";
    case EV_SUC_STARTED:
      return "EV_SUC_STARTED";
    case EV_INITIATE:
      return "EV_INITIATE";
    case EV_COMPLETE:
      return "EV_COMPLETE";
    case EV_TX_DONE_OK:
      return "EV_TX_DONE_OK";
    case EV_TX_DONE_FAIL:
      return "EV_TX_DONE_FAIL";
    case EV_TIMEOUT:
      return "EV_TIMEOUT";
    case EV_ADD_NODE_DONE:
      return "EV_ADD_NODE_DONE";
    case EV_S0_INCLUSION:
      return "EV_S0_INCLUSION";
    case EV_S0_INCLUSION_DONE:
      return "EV_S0_INCLUSION_DONE";
    default:
      sprintf(str, "%d", ev);
      return str;
  }
};

/**
 * @brief Get the string name of a handler state.
 *
 * @param state Handler state.
 * @return const char* Name of the state.
 */
const char *handler_state_name(int state)
{
  static char str[25];

  switch (state) {
    case STATE_IDLE:
      return "STATE_IDLE";
    case STATE_WAIT_FOR_SUC_ACCEPT:
      return "STATE_WAIT_FOR_SUC_ACCEPT";
    case STATE_WAIT_FOR_ADD:
      return "STATE_WAIT_FOR_ADD";
    case STATE_WAIT_S0_INCLUSION:
      return "STATE_WAIT_S0_INCLUSION";
    case STATE_PERFORM_S0_INCLUSION:
      return "STATE_PERFORM_S0_INCLUSION";
    default:
      sprintf(str, "%d", state);
      return str;
  }
};

/**
 * @brief Timeout callback for the handler FSM.
 *
 * @param user User data pointer (unused).
 */
static void timeout(void *user)
{
  (void)user; // Mark unused parameter
  handler_fsm(EV_TIMEOUT, 0);
}

/**
 * @brief Sleeptimer timeout callback.
 *
 * @param t Sleeptimer handle pointer.
 * @param u User data pointer.
 */
void sl_sleeptimer_timeout(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) t;
  timeout(u);
}

/**
 * @brief Callback for send done event.
 *
 * @param status Transmission status.
 * @param user   User data pointer.
 * @param t      Transmission status type pointer.
 */
static void send_done(uint8_t status, void *user, TX_STATUS_TYPE *t)
{
  (void)user; // Mark unused parameter
  (void)t;    // Mark unused parameter
  handler_fsm(status == TRANSMIT_COMPLETE_OK ? EV_TX_DONE_OK : EV_TX_DONE_FAIL,
              0);
}

/**
 * @brief Callback when S0 inclusion is done.
 *
 * @param status Status of S0 inclusion.
 */
static void sli_s0_inclusion_done(int status)
{
  handler_fsm(EV_S0_INCLUSION_DONE, &status);
}

static void sli_ic_suc_event_handler(handler_events_t ev, void *event_data)
{
  if (ev == EV_REQUEST_SUC_INCLUSION) {
    handler_state.state         = STATE_WAIT_FOR_SUC_ACCEPT;
    handler_state.complete_func = (inclusion_controller_cb_t) event_data;
    handler_state.suc_node      = ZW_GetSUCNodeID();

    ZW_INCLUSION_CONTROLLER_INITIATE_FRAME frame;
    ts_param_t ts;
    ts_set_std(&ts, handler_state.suc_node);

    frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
    frame.cmd      = INCLUSION_CONTROLLER_INITIATE;
    frame.node_id  = handler_state.node_id;
    frame.step_id  = handler_state.is_replace ? STEP_ID_PROXY_REPLACE
                     : STEP_ID_PROXY_INCLUSION;

    if (!sl_zw_send_data_appl(&ts, &frame, sizeof(frame), send_done, 0)) {
      send_done(TRANSMIT_COMPLETE_FAIL, 0, NULL);
    }
  } else if (ev == EV_INITIATE) {
    ZW_INCLUSION_CONTROLLER_INITIATE_FRAME *frame =
      (ZW_INCLUSION_CONTROLLER_INITIATE_FRAME *) event_data;
    handler_state.node_id = frame->node_id;

    handler_state.state = STATE_WAIT_FOR_ADD;
    //      StopNewNodeProbeTimer();

    if (frame->step_id == STEP_ID_PROXY_INCLUSION) {
      sl_nm_start_proxy_inclusion(frame->node_id);
    } else if (frame->step_id == STEP_ID_PROXY_REPLACE) {
      sl_nm_start_proxy_replace(frame->node_id);
    } else {
      handler_state.state = STATE_IDLE;
      ERR_PRINTF("invalid inclusion step.");
      return;
    }
  }
}

static void sli_ic_tx_done_event_handler(handler_events_t ev, void *event_data)
{
  if (ev == EV_TX_DONE_OK) {
    sl_sleeptimer_start_timer_ms(&handler_state.timer,
                                 CLOCK_SECOND * (60 * 4 + 2),
                                 sl_sleeptimer_timeout,
                                 0,
                                 1,
                                 0);
  } else if (ev == EV_COMPLETE) {
    ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME *frame =
      (ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME *) event_data;
    if ((frame->step_id == STEP_ID_PROXY_INCLUSION)
        || (frame->step_id == STEP_ID_PROXY_REPLACE)) {
      handler_state.state = STATE_IDLE;
      handler_state.complete_func(1);
    }
  } else if ((ev == EV_TX_DONE_FAIL) || (ev == EV_TIMEOUT)) {
    handler_state.state = STATE_IDLE;
    handler_state.complete_func(0);
  } else if (ev == EV_INITIATE) {
    ZW_INCLUSION_CONTROLLER_INITIATE_FRAME *frame =
      (ZW_INCLUSION_CONTROLLER_INITIATE_FRAME *) event_data;

    if (frame->step_id == STEP_ID_S0_INCLUSION) {
      handler_state.state = STATE_PERFORM_S0_INCLUSION;
      if (sl_get_cache_entry_flag(MyNodeID) & NODE_FLAG_SECURITY0) {
        security_add_begin(handler_state.node_id,
                           TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_EXPLORE
                           | TRANSMIT_OPTION_AUTO_ROUTE,
                           isNodeController(handler_state.node_id),
                           sli_s0_inclusion_done);
      } else {
        WRN_PRINTF("Cannot do S0 inclusion\n");
        sli_s0_inclusion_done(STEP_NOT_SUPPORTED);
      }
    }
  }
}

static void sli_ic_s0_done_event_handler(handler_events_t ev, void *event_data)
{
  (void)ev;         // Mark unused parameter
  (void)event_data; // Mark unused parameter

  if (EV_S0_INCLUSION_DONE) {
    ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME frame;
    ts_param_t ts;

    ts_set_std(&ts, handler_state.suc_node);
    ts.force_verify_delivery = TRUE;

    frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
    frame.cmd      = INCLUSION_CONTROLLER_COMPLETE;
    frame.step_id  = STEP_ID_S0_INCLUSION;
    frame.status   = *((int *) event_data);

    handler_state.state = STATE_WAIT_FOR_SUC_ACCEPT;

    if (!sl_zw_send_data_appl(&ts, &frame, sizeof(frame), send_done, 0)) {
      send_done(TRANSMIT_COMPLETE_FAIL, 0, NULL);
    }
  }
}

static void sli_ic_add_node_event_handler(handler_events_t ev, void *event_data)
{
  if (ev == EV_ADD_NODE_DONE) {
    ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME frame;
    ts_param_t ts;

    ts_set_std(&ts, handler_state.inclusion_conrtoller);
    ts.force_verify_delivery = TRUE;

    frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
    frame.cmd      = INCLUSION_CONTROLLER_COMPLETE;
    frame.step_id  = STEP_ID_PROXY_INCLUSION;
    frame.status   = *((uint8_t *) event_data);

    if (!sl_zw_send_data_appl(&ts, &frame, sizeof(frame), 0, 0)) {
      /* Dummy check. Code intentionally kept blank */
    }
    handler_state.state = STATE_IDLE;
  } else if (ev == EV_S0_INCLUSION) {
    ZW_INCLUSION_CONTROLLER_INITIATE_FRAME frame;
    ts_param_t ts;

    handler_state.complete_func = (inclusion_controller_cb_t) event_data;
    ts_set_std(&ts, handler_state.inclusion_conrtoller);

    frame.cmdClass = COMMAND_CLASS_INCLUSION_CONTROLLER;
    frame.cmd      = INCLUSION_CONTROLLER_INITIATE;
    frame.node_id  = handler_state.node_id;
    frame.step_id  = STEP_ID_S0_INCLUSION;

    if (!sl_zw_send_data_appl(&ts, &frame, sizeof(frame), send_done, 0)) {
      send_done(TRANSMIT_COMPLETE_FAIL, 0, NULL);
    }

    handler_state.state = STATE_WAIT_S0_INCLUSION;
    sl_sleeptimer_start_timer_ms(&handler_state.timer,
                                 CLOCK_SECOND * 30,
                                 sl_sleeptimer_timeout,
                                 0,
                                 1,
                                 0);
  }
}

static void sli_ic_timeout_event_handler(handler_events_t ev, void *event_data)
{
  if (ev == EV_TIMEOUT) {
    handler_state.state = STATE_WAIT_FOR_ADD;
    handler_state.complete_func(0);
  } else if (ev == EV_COMPLETE) {
    ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME *frame =
      (ZW_INCLUSION_CONTROLLER_COMPLETE_FRAME *) event_data;
    if (frame->step_id == STEP_ID_S0_INCLUSION) {
      handler_state.state = STATE_WAIT_FOR_ADD;
      handler_state.complete_func(frame->status);
    }
  }
}

/**
 * @brief Handler finite state machine for inclusion controller.
 *
 * @param ev         Event to process.
 * @param event_data Pointer to event data.
 */
static void handler_fsm(handler_events_t ev, void *event_data)
{
  // DBG_PRINTF("Inclusion Controller handler_fsm: state %s event %s\n",handler_state_name(handler_state.state), handler_event_name(ev));
  switch (handler_state.state) {
    case STATE_IDLE:
      sli_ic_suc_event_handler(ev, event_data);
      break;
    case STATE_WAIT_FOR_SUC_ACCEPT:
      sli_ic_tx_done_event_handler(ev, event_data);
      break;
    case STATE_PERFORM_S0_INCLUSION:
      sli_ic_s0_done_event_handler(ev, event_data);
      break;
    case STATE_WAIT_FOR_ADD:
      sli_ic_add_node_event_handler(ev, event_data);
      break;
    case STATE_WAIT_S0_INCLUSION:
      sli_ic_timeout_event_handler(ev, event_data);
      break;
  }
}

/**
 * @brief Z-Wave Inclusion Controller command handler.
 *
 * @param connection Pointer to Z-Wave connection structure.
 * @param frame      Pointer to received frame buffer.
 * @param length     Length of the received frame.
 * @return sl_command_handler_codes_t Status code.
 */
sl_command_handler_codes_t
inclusuion_controller_handler(zwave_connection_t *connection,
                              uint8_t *frame,
                              uint16_t length)
{
  (void)length; // Mark unused parameter

  nodeid_t sending_node = sl_node_of_ip(&connection->ripaddr);
  security_scheme_t highest_node_scheme =
    highest_scheme(sl_get_cache_entry_flag(sending_node));

  // As the inclusion controller command class opens of the non-secure nodes to
  // trigger handout of the security keys we do some explicit validation of the
  // sender:
  //  - The node sending is using its highest supported scheme
  //  - Probe of the requesting node must be done.
  //  - The node must not be known to have failed its secure inclusion.
  //  - The requester must be a controller.
  if (

    !scheme_compare(connection->scheme, highest_node_scheme)
    || (rd_get_node_probe_flags(sending_node)
        != RD_NODE_FLAG_PROBE_HAS_COMPLETED)
    || isNodeBad(sending_node)
    || !rd_check_nif_security_controller_flag(sending_node)) {
    WRN_PRINTF("Rejected inclusion controller command.");
    return COMMAND_HANDLED;
  }

  switch (frame[1]) {
    case INCLUSION_CONTROLLER_INITIATE:
      if (handler_state.state == STATE_IDLE) {
        handler_state.inclusion_conrtoller = sending_node;
      }
      handler_fsm(EV_INITIATE, frame);
      break;
    case INCLUSION_CONTROLLER_COMPLETE:
      handler_fsm(EV_COMPLETE, frame);
      break;
    default:
      return COMMAND_NOT_SUPPORTED;
  }
  return COMMAND_HANDLED;
}

/**
 * @brief Triggers S0 inclusion via the handler FSM.
 *
 * @param complete_func Callback function to call when inclusion is complete.
 */
void inclusion_controller_you_do_it(inclusion_controller_cb_t complete_func)
{
  handler_fsm(EV_S0_INCLUSION, complete_func);
}

/**
 * @brief Sends a report to the handler FSM.
 *
 * @param status Inclusion controller status.
 */
void inclusion_controller_send_report(inclusion_cotroller_status_t status)
{
  handler_fsm(EV_ADD_NODE_DONE, &status);
}

/**
 * @brief Notifies the handler FSM that inclusion controller has started.
 */
void inclusion_controller_started(void)
{
  handler_fsm(EV_SUC_STARTED, 0);
}

/**
 * @brief Requests handover to the inclusion controller.
 *
 * @param node_id       Node ID to include.
 * @param is_replace    1 if replace, 0 if not.
 * @param complete_func Callback function to call when complete.
 */
void request_inclusion_controller_handover(
  nodeid_t node_id,
  uint8_t is_replace,
  inclusion_controller_cb_t complete_func)
{
  if (handler_state.state != STATE_IDLE && complete_func) {
    complete_func(0);
  } else {
    handler_state.node_id    = node_id;
    handler_state.is_replace = is_replace;
    handler_fsm(EV_REQUEST_SUC_INCLUSION, complete_func);
  }
}

/**
 * @brief Initializes the inclusion controller state.
 */
void controller_inclusuion_init()
{
  handler_state.state = STATE_IDLE;
}
