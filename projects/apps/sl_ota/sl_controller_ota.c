/*******************************************************************************
 * @file  sl_controller_ota.c
 * @brief OTA functions
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
/*************************************************************************
 *
 */

#include "stdio.h"
#include "stdbool.h"
#include "cmsis_os2.h"
#include "Serialapi.h"
#include "sl_status.h"
#include "sl_uart_drv.h"
#include "sl_controller_ota.h"
#include "sl_common_log.h"
#include "modules/sl_psram.h"
#include "utls/zgw_crc.h"
#include "SerialAPI/sl_serial.h"

#define OTA_CONTROLLER_EVENT_FLAG (1U << 0)
#define HIGHEST_BIT_FLAG (1U << 31U)

#define X_MODEM_PAYLOAD_SIZE 128
#define X_MODEM_PKT_MAX_SIZE 133

#define  SOH  0x01 /*   Start of Header  */
#define  EOT  0x04 /*   End of Transmission */
#define  TX_MODEM_ACK  0x06 /*   Acknowledge  */
#define  NAK  0x15 /*   Not Acknowledge */
#define  CAN  0x18 /*   Cancel (Force receiver to start sending C's) */
#define  C    0x43 /*   ASCII "C" */

/****************************************************************************/
/*                            LOCAL VARIABLES                               */
/****************************************************************************/

uint8_t *sl_txmodem_buf = NULL;
int pktNumber = 0;
int didx = 0;
int end;
int step;
uint8_t retry = 15;

static uint32_t sli_controller_img_size = 0;

/****************************************************************************/
/*                            PRIVATE FUNCTIONS                             */
/****************************************************************************/

static void send_data(const uint8_t *data, int len)
{
  int i = 0;
  uint16_t crc = 0;
  int chunk_size = (((didx + X_MODEM_PAYLOAD_SIZE) > len)
                    ? (len % X_MODEM_PAYLOAD_SIZE) : X_MODEM_PAYLOAD_SIZE);

  LOG_PRINTF("send_data %d %d %d\n", didx, len, pktNumber);
  LOG_PRINTF("Sent %d bytes, remaining:%d bytes\n", didx, len - didx);
  if (!(didx % step)) {
    LOG_PRINTF(">");
  }
  if (sl_txmodem_buf[0] == EOT) {
    goto send_ETB_EOT;
  }

  sl_txmodem_buf[i++] = SOH;
  sl_txmodem_buf[i++] = pktNumber;
  sl_txmodem_buf[i++] = ~pktNumber;
  memcpy(&sl_txmodem_buf[i], &data[didx], chunk_size);
  crc = zgw_crc16(0, &sl_txmodem_buf[i], chunk_size);
  i += chunk_size;
  sl_txmodem_buf[i++] = (crc >> 8) & 0xff;
  sl_txmodem_buf[i] = (crc) & 0xff;

  if (i > X_MODEM_PKT_MAX_SIZE) {
    ERR_PRINTF("Sending more than X_MODEM_PKT_MAX_SIZE?\n");
  }
  LOG_PRINTF("sent offset :%d\n", didx);
  if ((didx + X_MODEM_PAYLOAD_SIZE) > len) {
    didx = len;   /* last chunk */
  } else {
    didx += X_MODEM_PAYLOAD_SIZE;
  }
  LOG_PRINTF("new offset :%d\n", didx);

  pktNumber++;
  send_ETB_EOT:
  LOG_PRINTF("sending %d bytes:", i + 1);
  LOG_PRINTF(" 0x%02x 0x%02x 0x%02x\n", sl_txmodem_buf[0],
             sl_txmodem_buf[1], sl_txmodem_buf[2]);

  sl_uart_drv_send_buf(sl_txmodem_buf, i + 1);
  osDelay(1); // wait for the data to be sent
}

static uint8_t recv_byte()
{
  /* loop until there is something */
  uint8_t c;

  LOG_PRINTF("Enter C(67) ACK(1) NAK (2)\n");
  sl_serial_read_byte_block(&c);
  LOG_PRINTF("Received: %x %c\n", c, c);
  if (c == '1') {
    c = TX_MODEM_ACK;
  } else if (c == '2') {
    c = NAK;
  }

  return c;
}

static uint8_t xmodem_tx()
{
  uint8_t *data = NULL;
  uint32_t old_controller_img_size = 0;
  int len;

  bool bypass_c_flag = false;

  data = (uint8_t*)PSRAM_CONTROLLER_IMG_BASE_ADDRESS;

  if (sli_controller_img_size % X_MODEM_PAYLOAD_SIZE) {
    old_controller_img_size = sli_controller_img_size;
    sli_controller_img_size = sli_controller_img_size
                              + (128 - (sli_controller_img_size % 128));
    LOG_PRINTF("sli_controller_img_size: %ld, old_controller_img_size: %ld\n\r",
               sli_controller_img_size, old_controller_img_size);
    memset(&data[old_controller_img_size], 0,
           sli_controller_img_size - old_controller_img_size);
  }

  len = sli_controller_img_size;
  end = sli_controller_img_size  / X_MODEM_PAYLOAD_SIZE;
  step = end / 50;

  while (retry) {
    uint8_t rb = recv_byte();
    switch (rb) {
      case C:
        LOG_PRINTF("Received C\n");

        //  Fix receive 2 C character to remove this variable
        if (bypass_c_flag) {
          break;
        }

        DBG_PRINTF("Sending Firmware file to Gecko module\n");

        if (pktNumber != 0) {
          ERR_PRINTF("Received C in the middle of Transmission?\n");
          LOG_PRINTF("\n");
          pktNumber = 0;
          didx = 0;
          return 0;
        }
        pktNumber = 1;
        didx = 0;
        send_data(data, len);
        retry = 15;

        bypass_c_flag = true;

        break;
      case TX_MODEM_ACK: /*send next pkt */
        if (didx == 0) {  //That means we haven't even started and we got ACK from XMODEM Receiver. Only C is expected here
          ERR_PRINTF("Unexpected ACK\n");
          LOG_PRINTF("\n");
          pktNumber = 0;
          didx = 0;
          return 0;
        }

        if (sl_txmodem_buf[0] == EOT) {
          sl_txmodem_buf[0] = 0;
          pktNumber = 0;
          didx = 0;
          LOG_PRINTF("\n");
          return 1;
        }

        if (didx == len) {
          sl_txmodem_buf[0] = EOT;
          LOG_PRINTF("sending EOT\n");
        }

        send_data(data, len);
        retry = 15;
        break;
      case NAK: /* send same pkt */
        if (didx == 0) {  //That means we haven't even started and we got NAK from XMODEM Receiver. Only C is expected here
          ERR_PRINTF("Unexpected NAK\n\r");
          LOG_PRINTF("\n");
          pktNumber = 0;
          didx = 0;
          return 0;
        }
        ERR_PRINTF("Received NAK\n\r");
        pktNumber--;
        retry--;
        didx -= ((didx == len) ? (len % X_MODEM_PAYLOAD_SIZE) : X_MODEM_PAYLOAD_SIZE);
        LOG_PRINTF("Resending offset %d\n", didx);
        send_data(data, len);
        break;
      case CAN:
        LOG_PRINTF("\n");
        ERR_PRINTF("Received CAN");
        pktNumber = 0;
        didx = 0;
        return 0;
      default:
        retry--;
        LOG_PRINTF("unknown char received %x\n", rb);
        break;
    }
  }
  pktNumber = 0;
  didx = 0;
  return 0;
}

/****************************************************************************/
/*                            PUBLIC FUNCTIONS                              */
/****************************************************************************/

sl_status_t sl_start_controller_ota()
{
  uint8_t c = 0;

  if (!sli_controller_img_size) {
    ERR_PRINTF("No controller img in PSRAM\r\n");
    return SL_STATUS_FAIL;
  }
  sl_serial_api_enter_ota_mode(true);

  ZW_AutoProgrammingEnable();
  LOG_PRINTF("Auto Programming enabled\n");
  osDelay(1000);

  /* Wait for following msg from Bootloader */
  /*Gecko Bootloader v1.5.1
   * 1. upload gbl
   * 2. run
   * 3. ebl info
   * BL >
   */

  uint32_t s_time = osKernelGetTickCount(); // 10 seconds timeout
  while ( c != '>') {
    if (sl_serial_read_byte_block(&c) > 0) {
    }
    if (osKernelGetTickCount() - s_time > 10000) { // 10 seconds timeout
      LOG_PRINTF("Timeout waiting for bootloader prompt\n");
      goto ota_ctrl_exit;
    }
  }

  LOG_PRINTF("1\n");
  sl_uart_drv_put_char('1');

  c = 0;
  s_time = osKernelGetTickCount(); // 10 seconds timeout
  while ( c != 'd') { // wait for "begin upload"
    sl_serial_read_byte_block(&c);

    if (osKernelGetTickCount() - s_time > 10000) { // 10 seconds timeout
      LOG_PRINTF("Timeout waiting for bootloader prompt\n");
      goto ota_ctrl_exit;
    }
  }

  if (!xmodem_tx()) {
    LOG_PRINTF("Error in transmission\n");
    goto ota_ctrl_exit;
  } else {
    LOG_PRINTF("Success in transmission\n");
    c = 0;
    s_time = osKernelGetTickCount(); // 10 seconds timeout
    while ( c != '>') {
      sl_serial_read_byte_block(&c);
      if (osKernelGetTickCount() - s_time > 10000) { // 10 seconds timeout
        LOG_PRINTF("Timeout waiting for bootloader prompt\n");
        goto ota_ctrl_exit;
      }
    }

    LOG_PRINTF("Running the uploaded gbl on 700/800-series chip\n");
    sl_uart_drv_put_char('2');
    return SL_STATUS_OK;
  }

  ota_ctrl_exit:
  sl_serial_api_enter_ota_mode(false);

  return SL_STATUS_FAIL;
}

void sl_store_controller_ota_img_size(uint32_t controller_img_size)
{
  sli_controller_img_size = controller_img_size;
  sl_txmodem_buf = sl_get_tx_buf();
}

void sl_ota_init()
{
  sl_txmodem_buf = sl_get_tx_buf();
}
