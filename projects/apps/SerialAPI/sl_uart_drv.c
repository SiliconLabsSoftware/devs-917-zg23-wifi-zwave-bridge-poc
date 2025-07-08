/***************************************************************************/ /**
 * @file sl_uart_drv.c
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

#include "FreeRTOS.h"
#include "queue.h"
#include "sl_uart_drv.h"
#include "sl_si91x_usart.h"
#include "rsi_debug.h"
#include "rsi_common_apis.h"
#include "sl_common_log.h"
#include "lib/ringbuf.h"

#define SL_RB_MASK_BYTES 128

#define SERIAL_API_BAUD_RATE 115200

/*******************************************************************************
 ***************************  LOCAL VARIABLES   ********************************
 ******************************************************************************/

struct ringbuf sl_rb_drv;
uint8_t sl_rb_pool[SL_RB_MASK_BYTES + 1];

/*******************************************************************************
 **************************   PRIVATE FUNCTIONS   ******************************
 ******************************************************************************/
extern ARM_DRIVER_USART Driver_USART0;
ARM_DRIVER_USART *serial_api_drv      = &Driver_USART0;
volatile uint32_t sl_serial_send_done = 0;
volatile uint32_t sl_serial_recv_done = 0;
uint8_t sl_serial_rx_char;

/**
 * @brief USART event callback handler.
 *
 * Handles USART events such as send/receive complete and errors.
 *
 * @param[in] event Event mask from USART driver.
 */
void sl_serial_api_SignalEvent(uint32_t event)
{
  // Get the USART Event
  event &= USART_EVENT_MASK;
  switch (event) {
    case ARM_USART_EVENT_SEND_COMPLETE:
      sl_serial_send_done++;
      break;
    case ARM_USART_EVENT_RECEIVE_COMPLETE:
      ringbuf_put(&sl_rb_drv, sl_serial_rx_char);
      serial_api_drv->Receive((void *) &sl_serial_rx_char, 1);
      sl_serial_recv_done++;
      break;
    case ARM_USART_EVENT_TRANSFER_COMPLETE:
      break;
    case ARM_USART_EVENT_TX_COMPLETE:
      break;
    case ARM_USART_EVENT_TX_UNDERFLOW:
      break;
    case ARM_USART_EVENT_RX_OVERFLOW:
      break;
    case ARM_USART_EVENT_RX_TIMEOUT:
      break;
    case ARM_USART_EVENT_RX_BREAK:
      break;
    case ARM_USART_EVENT_RX_FRAMING_ERROR:
      break;
    case ARM_USART_EVENT_RX_PARITY_ERROR:
      break;
    case ARM_USART_EVENT_CTS:
      break;
    case ARM_USART_EVENT_DSR:
      break;
    case ARM_USART_EVENT_DCD:
      break;
    case ARM_USART_EVENT_RI:
      break;
    default:
      // Handle unexpected events
      break;
  }
}

/**
 * @brief Initialize the serial API driver and ring buffer.
 *
 * Sets up the USART hardware and initializes the receive ring buffer.
 *
 * @return int 1 if initialization is successful.
 */
int sl_serial_api_drv_init(void)
{
  serial_api_drv->Uninitialize();

  serial_api_drv->Initialize(sl_serial_api_SignalEvent);

  serial_api_drv->PowerControl(ARM_POWER_FULL);

  /* Enable Receiver and Transmitter lines */
  serial_api_drv->Control(ARM_USART_CONTROL_TX, 1);

  serial_api_drv->Control(ARM_USART_CONTROL_RX, 1);

  serial_api_drv->Control(ARM_USART_MODE_ASYNCHRONOUS | ARM_USART_DATA_BITS_8
                          | ARM_USART_PARITY_NONE | ARM_USART_STOP_BITS_1
                          | ARM_USART_FLOW_CONTROL_NONE,
                          SERIAL_API_BAUD_RATE);

  ringbuf_init(&sl_rb_drv, sl_rb_pool, SL_RB_MASK_BYTES);
  serial_api_drv->Receive((void *) &sl_serial_rx_char, 1);
  return 1;
}

/**
 * @brief Send a buffer of bytes over the serial interface.
 *
 * @param[in] ptr Pointer to the data buffer to send.
 * @param[in] len Number of bytes to send.
 */
void sl_serial_api_drv_puts(const uint8_t *ptr, uint16_t len)
{
  for (int i = 0; i < len; i++) {
    uint32_t t = osKernelGetTickCount();
    sl_serial_send_done = 0;
    serial_api_drv->Send(&ptr[i], 1);
    while ((sl_serial_send_done == 0) && (osKernelGetTickCount() - t < 10))
      ;
  }
  return;
}

/**
 * @brief Send a single byte over the serial interface.
 *
 * @param[in] c Byte to send.
 */
void sl_serial_api_drv_putc(uint8_t c)
{
  uint32_t t = osKernelGetTickCount();
  sl_serial_send_done = 0;
  serial_api_drv->Send(&c, 1);
  while ((sl_serial_send_done == 0) && (osKernelGetTickCount() - t < 10)) {
    RSI_M4SSUsart0Handler();
  }
  return;
}

/**
 * @brief Retrieve bytes from the UART receive buffer.
 *
 * @param[out] buf Pointer to buffer to store received bytes.
 * @param[in] len  Maximum number of bytes to retrieve.
 * @return int32_t Number of bytes actually retrieved.
 */
int32_t sl_uart_drv_get_buf(uint8_t *buf, uint8_t len)
{
  int c      = ringbuf_elements(&sl_rb_drv);
  uint8_t *p = buf;
  if (c == 0) {
    return 0;
  }
  int32_t r_len = (len > c) ? c : len;
  for (int i = 0; i < r_len; i++) {
    int r = ringbuf_get(&sl_rb_drv);
    *p++  = (uint8_t) (r & 0xFF);
  }
  return r_len;
}

/**
 * @brief Retrieve a single byte from the UART receive buffer.
 *
 * @param[out] ch Pointer to variable to store the received byte.
 * @return int32_t 1 if a byte was retrieved, -1 if buffer is empty.
 */
int32_t sl_uart_drv_get_char(uint8_t *ch)
{
  int c = ringbuf_get(&sl_rb_drv);
  if (c < 0) {
    return -1;
  }
  *ch = (uint8_t) (c & 0xFF);
  return 1;
}

/**
 * @brief Get the number of bytes currently in the UART receive buffer.
 *
 * @return int32_t Number of bytes available in the receive buffer.
 */
int32_t sl_uart_rx_buf_count(void)
{
  return ringbuf_elements(&sl_rb_drv);
}
