/*******************************************************************************
 * @file  sl_tcpip_handler.c
 * @brief handle data from tcpip stack.
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

#include "FreeRTOS.h"
#include "task.h"
#include "Common/sl_common_log.h"
#include "Common/sl_gw_info.h"
#include "Common/sl_tcpip_def.h"

#include "Z-Wave/include/ZW_classcmd.h"
#include "Z-Wave/include/ZW_transport_api.h"
#include "SerialAPI/Serialapi.h"

#include "transport/sl_zw_frm.h"
#include "transport/sl_zw_send_data.h"
#include "transport/sl_zw_send_request.h"

#include "utls/ipv6_utils.h"
#include "utls/sl_ipnode_utils.h"

#include "ip_bridge/sl_classic_zip_node.h"

#include "sl_ts_thread.h"

const osThreadAttr_t sl_tcpip_attr = {
  .name       = "tcpip_t",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 8192,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

static osMessageQueueId_t sli_tcpip_queue;

#define zw_send_lock()   // osMutexAcquire(sli_tcpip_mutex, osWaitForever)
#define zw_send_unlock() // osMutexRelease(sli_tcpip_mutex)

static bool zw_sending = false;

sl_sleeptimer_timer_handle_t zw_sending_timer;

/*===========================================================================*/
/**
 * @brief .
 *
 * @param[in] none
 * @return void
 * @note
 */
sl_status_t zw_tcpip_post_event(uint32_t event, void *data)
{
  sl_status_t status;
  sl_cc_net_ev_t msg = { .ev = event, .ev_data = data };

  status = osMessageQueuePut(sli_tcpip_queue, (void *) &msg, 0, osWaitForever);
  return status;
}

/*===========================================================================*/
/**
 * @brief .
 *
 * @param[in] none
 * @return void
 * @note
 */
sl_status_t zw_tcpip_get_event(void *msg, uint32_t timeout)
{
  if (osMessageQueueGet(sli_tcpip_queue, msg, NULL, timeout) == osOK) {
    return SL_STATUS_OK;
  }
  return SL_STATUS_TIMEOUT;
}

const char *transmit_status_name(int state)
{
  switch (state) {
    case TRANSMIT_COMPLETE_OK:
      return "TRANSMIT_COMPLETE_OK";
    case TRANSMIT_COMPLETE_NO_ACK:
      return "TRANSMIT_COMPLETE_NO_ACK";
    case TRANSMIT_COMPLETE_FAIL:
      return "TRANSMIT_COMPLETE_FAIL";
    case TRANSMIT_ROUTING_NOT_IDLE:
      return "TRANSMIT_ROUTING_NOT_IDLE";
    case TRANSMIT_COMPLETE_REQUEUE_QUEUED:
      return "TRANSMIT_COMPLETE_REQUEUE_QUEUED";
    case TRANSMIT_COMPLETE_REQUEUE:
      return "TRANSMIT_COMPLETE_REQUEUE";
    case TRANSMIT_COMPLETE_ERROR:
      return "TRANSMIT_COMPLETE_ERROR";
    default:
      return "Unknown";
  }
}

static void queue_send_done(BYTE status, BYTE *sent_buffer, uint16_t send_len)
{
  (void) sent_buffer; // Unused parameter
  (void) send_len;    // Unused parameter
  zw_sending = false;
  zw_send_unlock();

  LOG_PRINTF("queue_send_done to node status: %s\n",
             transmit_status_name(status));

  if (status == TRANSMIT_COMPLETE_OK) {
    // Reserved for future use.
  }

  if (status == TRANSMIT_COMPLETE_REQUEUE) {
    return;
  }

  if (status == TRANSMIT_COMPLETE_REQUEUE_QUEUED) {
    /* This happens if NetworkManagement or ResourceDirectory are busy
     * or if (Mailbox is sending and this frame does not have mailbox
     * or re-q flag set). */
    /* Frames for sleeping nodes must be queued in mailbox.  All other
     * frames are sent to long queue. */
    return;
  }
}

void sl_zw_sending_timeour_cb(sl_sleeptimer_timer_handle_t *t, void *d)
{
  (void) t; // Unused parameter
  (void) d; // Unused parameter
  zw_sending = false;
  printf("zw_sending out\n");
}

void sl_tcpip_thread(void *arg)
{
  (void) arg; // Unused parameter
  sl_cc_net_ev_t msg;
  nodeid_t node;
  BOOL already_requeued = 0;

  while (1) {
    // wait a event.
    if (!zw_sending && zw_tcpip_get_event(&msg, /*osWaitForever*/ 1) == osOK) {
      zw_sending = true;
      // process event;
      sl_tcpip_buf_t *tcpip_buf = (sl_tcpip_buf_t *) msg.ev_data;

      DBG_PRINTF("\nTIME: %ld, lipaddr: ", xTaskGetTickCount());
      uip_debug_ipaddr_print(&tcpip_buf->zw_con.lipaddr);
      DBG_PRINTF("\n");
      DBG_PRINTF("\nripaddr: ")
      uip_debug_ipaddr_print(&tcpip_buf->zw_con.ripaddr);
      DBG_PRINTF("\n");
      node = sl_node_of_ip(&(tcpip_buf->zw_con.lipaddr));
      DBG_PRINTF("destination: %d\n", node);

      // backup tcpip buf
      sl_backup_uip_buf(tcpip_buf);
      sl_backup_zip_buf(tcpip_buf->zip_data, tcpip_buf->zip_data_len); // pass node id.

      if (!ClassicZIPNode_input(node,
                                queue_send_done,
                                FALSE,
                                already_requeued)) {
        ERR_PRINTF("ClassicZIPNode_input: return error.\n");
        queue_send_done(TRANSMIT_COMPLETE_FAIL, 0, 0);
      }
      DBG_PRINTF("TIME: %ld\n",
                 xTaskGetTickCount());
      // consider wait for zwave frame to be sent.
      /* free data*/
      if (tcpip_buf->zip_data) {
        free(tcpip_buf->zip_data);
      }

      if (msg.ev_data) {
        free(msg.ev_data);
      }
    }
    sl_zw_layer_data_process();
    osDelay(5);
  }
}

void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
  (void) xTask;        // Unused parameter
  (void) pcTaskName;   // Unused parameter
  /* Handle stack overflow here (e.g., log, halt, reset) */
  LOG_PRINTF("Stack overflow in task: %s\n", pcTaskName);
  taskDISABLE_INTERRUPTS();
  while (1) {
    // halt.
  }
}

void sl_tcpip_init(void)
{
  LOG_PRINTF("tcpip thread start\n");
  sli_tcpip_queue = osMessageQueueNew(20, sizeof(sl_cc_net_ev_t), NULL);
  if (!osThreadNew((osThreadFunc_t) sl_tcpip_thread, NULL, &sl_tcpip_attr)) {
    LOG_PRINTF("tcpip thread start FAIL!!!!\n");
  }
}
