/***************************************************************************/ /**
 * @file sl_serial.c
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
#include "cmsis_os2.h"
#include "sl_serial.h"
#include "sl_sleeptimer.h"
#include "sl_uart_drv.h"
#include "rsi_common_apis.h"
#include <stdbool.h>
#include <string.h>
#include "sl_common_log.h"

#define RX_ACK_TIMEOUT_DEFAULT_MS  10
#define RX_BYTE_TIMEOUT_DEFAULT_MS 260

/****************************************************************************/
/*                      PRIVATE TYPES and DEFINITIONS                       */
/****************************************************************************/
/* serial Protocol handler states */
typedef enum {
  stateSOFHunt  = 0,
  stateLen      = 1,
  stateType     = 2,
  stateCmd      = 3,
  stateData     = 4,
  stateChecksum = 5
} T_CON_TYPE;

/****************************************************************************/
/*                              PRIVATE DATA                                */
/****************************************************************************/
static uint8_t sl_ser_buf[SERBUF_MAX];
static uint8_t sl_ser_buf_len;

static uint8_t con_state;
static uint8_t sl_checksum;

static bool sl_rx_is_active     = false;
static char sl_ack_nack_needed  = false;
static uint8_t sl_timer_expired = 0;
static uint8_t sl_rx_cancel     = 0;

/**
 * @brief Read a single byte from the UART with a timeout.
 *
 * @param[out] ch Pointer to the variable where the received byte will be stored.
 * @return int 1 if a byte was read successfully, -1 if timeout occurred.
 */
int sl_serial_read_byte_block(uint8_t *ch)
{
  uint32_t t = osKernelGetTickCount();
  while ((osKernelGetTickCount() - t) < RX_ACK_TIMEOUT_DEFAULT_MS) {
    int ret = sl_uart_drv_get_char(ch);
    if (ret > 0) {
      return 1;
    }
    osDelay(1);
  }
  return -1;
}

/**
 * @brief Read a buffer of bytes from the UART with a timeout.
 *
 * @param[out] buf   Pointer to the buffer where received data will be stored.
 * @param[in]  rlen  Number of bytes to read.
 * @return int Number of bytes actually read. Can be less than @p rlen if timeout occurs.
 */
int sl_serial_read_buf_block(uint8_t *buf, uint32_t rlen)
{
  uint32_t t = osKernelGetTickCount();
  uint32_t len = 0;
  while ((osKernelGetTickCount() - t) < RX_BYTE_TIMEOUT_DEFAULT_MS) {
    len = sl_uart_rx_buf_count();
    if (len >= rlen) {
      sl_uart_drv_get_buf(buf, rlen);
      return rlen;
    }
    osDelay(1);
  }
  sl_uart_drv_get_buf(buf, len);
  return len;
}

static uint8_t sli_ser_calc_checksum(uint8_t *tx_buffer, uint16_t len)
{
  uint8_t bChecksum = 0xFF;
  for (uint16_t i = 0; i < len; i++) {
    bChecksum ^= tx_buffer[i];
  }
  return bChecksum;
}

static enum T_CON_TYPE sli_ser_rx_fsm(uint8_t ch, uint8_t acknowledge)
{
  enum T_CON_TYPE retVal = conIdle;
  switch (con_state) {
    case stateSOFHunt:
      if (ch == F_SOF) {
        sl_checksum     = 0xff;
        sl_ser_buf_len  = 0;
        sl_rx_is_active = 1; // now we're receiving - check for timeout
        con_state++;
        break;
      }

      if (sl_ack_nack_needed) {
        if (ch == F_ACK) {
          retVal             = conFrameSent;
          sl_ack_nack_needed = 0; // Done
        } else if (ch == F_NAK) {
          retVal             = conTxErr;
          sl_ack_nack_needed = 0;
        } else if (ch == F_CAN) {
          retVal             = conTxWait;
          sl_ack_nack_needed = 0;
        } else {
          // Bogus character received...
        }
      }
      break;
    case stateLen:
      if ((ch < FRAME_LENGTH_MIN) || (ch == FRAME_LENGTH_MAX)) {
        con_state       = stateSOFHunt; // Restart looking for SOF
        sl_rx_is_active = 0;            // Not really active now...
        break;
      }
      sl_ser_buf[0]  = ch;
      sl_ser_buf_len = ch;
      // enable rx timeout
      uint8_t *p_data = sl_ser_buf + 1;
      sl_rx_cancel = sl_serial_read_buf_block(p_data, sl_ser_buf_len);

      SL_LOG_PRINT("rx raw: %d bytes\n", sl_ser_buf_len + 1);
      sl_checksum = sli_ser_calc_checksum(sl_ser_buf, sl_ser_buf_len + 1);

      if (acknowledge) {
        if (sl_checksum == 0) {
          sl_uart_drv_put_char(F_ACK);
          sl_uart_drv_flush();       // Start sending frame to the host.
          retVal = conFrameReceived; // Tell THE world that we got a packet
        } else {
          sl_uart_drv_put_char(F_NAK); // Tell them something is wrong...
          sl_uart_drv_flush();         // Start sending frame to the host.
          retVal = conFrameErr;
          SL_LOG_PRINT("*** CRC ERROR not enough! %d/%d\n", sl_rx_cancel, sl_ser_buf_len);
        }
      } else {
        // We are in the process of looking for an acknowledge to a callback
        // request Drop the new frame we received - we don't have time to handle
        // it. Send a CAN to indicate what is happening...
        sl_uart_drv_put_char(F_CAN);
        sl_uart_drv_flush(); // Start sending frame to the host.
      }
      con_state       = stateSOFHunt; // Restart looking for SOF
      sl_rx_is_active = 0;            // Not really active now...
      break;
    default:
      con_state       = stateSOFHunt; // Restart looking for SOF
      sl_rx_is_active = 0;            // Not really active now...
      break;
  }
  return retVal;
}

/****************************************************************************/
/*                           EXPORTED FUNCTIONS                             */
/****************************************************************************/

/*===============================   sl_serial_tx_frame   =============================
**
**   Transmit frame via serial port by adding SOF, Len, Type, Cmd and Checksum.
**   Frame format: SOF-Len-Type-Cmd-DATA-Checksum
**
** @param[in] cmd Command byte.
** @param[in] type Frame type to send (Response or Request).
** @param[in] Buf Pointer to data buffer to send.
** @param[in] len Length of data buffer.
**
**--------------------------------------------------------------------------*/
void /*RET Nothing */
sl_serial_tx_frame(
  uint8_t cmd,        /* IN Command */
  uint8_t type,       /* IN frame Type to send (Response or Request) */
  uint8_t *Buf,       /* IN pointer to uint8_t buffer containing DATA to send */
  uint8_t len)        /* IN the length of DATA to transmit */
{
  uint8_t tx_buffer[255];
  uint8_t *c;
  uint8_t bChecksum;
  c = tx_buffer;

  *c++ = F_SOF;
  *c++ = len + 3;
  *c++ = type;
  *c++ = cmd;

  memcpy(c, Buf, len);
  c += len;

  bChecksum = sli_ser_calc_checksum(tx_buffer + 1, len + 3);
  *c++ = bChecksum;
  LOG_PRINTF("tx: %d bytes\n", len + 5);
  sl_uart_drv_send_buf((const uint8_t*)tx_buffer, len + 5);
  sl_ack_nack_needed = 1; // Now we need an ACK...
}

/*==============================   sl_serial_rx_frame   =============================
**
**   Parses serial data sent from external controller module (PC-controller).
**   Should be frequently called by main loop.
**
** @param[in] acknowledge If non-zero, send ACK/NAK as appropriate.
** @return enum T_CON_TYPE Connection state after processing.
**
**--------------------------------------------------------------------------*/
enum T_CON_TYPE /*RET conState - See above */
sl_serial_rx_frame(
  uint8_t acknowledge)       /* IN do we send acknowledge and handle
                                           frame if received correctly */
{
  uint8_t ch;
  enum T_CON_TYPE retVal = conIdle;
  // query data.
  do {
    sl_timer_expired = 0;
    int32_t ret = sl_serial_read_byte_block(&ch);
    if (ret < 0) {
      sl_timer_expired = 1;
      break;
    }
    retVal = sli_ser_rx_fsm(ch, acknowledge);

    /* FIXME The second call to SerialCheck can be removed,
     * since we already read the whole frame. */
  } while ((retVal == conIdle));

  /* Check for timeouts - if no other events detected */
  if (retVal == conIdle) {
    /* Are in the middle of collecting a frame and have we timed out? */
    if (sl_rx_is_active && (sl_timer_expired == 1)) {
      /* Reset to SOF hunting */
      con_state       = stateSOFHunt;
      sl_rx_is_active = 0; /* Not inframe anymore */
      retVal          = conRxTimeout;
    }
    /* Are we waiting for ACK and have we timed out? */
    if (sl_ack_nack_needed && (sl_timer_expired == 1)) {
      /* Not waiting for ACK anymore */
      sl_ack_nack_needed = 0;
      /* Tell upper layer we could not get the frame through */
      retVal = conTxTimeout;
    }
  }
  return retVal;
}

/*==============================   sl_serial_init   =============================
**
**   Initialize the module.
**
**--------------------------------------------------------------------------*/
int                                         /*RET  Nothing */
sl_serial_init(const char *serial_port)     /*IN   Nothing */
{
  (void) serial_port;
  uint8_t rc;
  sl_ser_buf_len = 0;
  con_state      = stateSOFHunt;

  rc = sl_serial_api_drv_init();
  if (rc) {
    /*Send ACK in case that the target expects one.*/
    sl_uart_drv_put_char(F_ACK);
  }
  return rc;
}

void sl_serial_destroy()
{
  sl_uart_drv_deinit();
}

uint32_t sl_serial_get_buf(uint8_t *buf_out)
{
  memcpy(buf_out, sl_ser_buf, sl_ser_buf_len);
  return sl_ser_buf_len;
}

uint8_t *sl_serial_get_local_buf_data(void)
{
  return &sl_ser_buf[0];
}

uint8_t sl_serial_get_local_buf_len(void)
{
  return sl_ser_buf_len;
}

void sl_serial_set_local_buf(uint8_t *src_buf, uint16_t len)
{
  sl_ser_buf_len = len;
  memcpy(sl_ser_buf, src_buf, len);
}

const char *sl_serial_get_state_name(enum T_CON_TYPE t)
{
  switch (t) {
    case conIdle: // returned if nothing special has happened
      return "conIdle";
    case conFrameReceived: // returned when a valid frame has been received
      return "conFrameReceived";
    case conFrameSent: // returned if frame was ACKed by the other end
      return "conFrameSent";
    case conFrameErr: // returned if frame has error in Checksum
      return "conFrameErr";
    case conRxTimeout: // returned if Rx timeout has happened
      return "conRxTimeout";
    case conTxTimeout: // returned if Tx timeout (waiting for ACK) ahs happened
      return "conTxTimeout";
    case conTxErr: // We got a NAK after sending
      return "conTxErr";
    case conTxWait: // We got a CAN while sending
      return "conTxWait";
  }
  return NULL;
}
