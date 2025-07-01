/*******************************************************************************
 * @file  sl_uart_drv.h
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
#ifndef SL_UART_DRV_H
#define SL_UART_DRV_H

#include "stdint.h"
#include "rsi_usart.h"

/**
 * @brief Initializes the UART peripheral.
 *
 * @param usart_instance The USART instance.
 * @return uint8_t 1 on success, 0 on failure.
 */
uint8_t sl_uart_drv_init(usart_peripheral_t usart_instance);

/**
 * @brief De-initializes the UART peripheral.
 *
 * @return uint8_t 1 on success, 0 on failure.
 */
uint8_t sl_uart_drv_deinit(void);

/**
 * @brief Sends a buffer of data over UART.
 *
 * @param[in] buf Pointer to the data buffer.
 * @param[in] len Number of bytes to send.
 */
//void sl_uart_drv_send_buf(uint8_t* buf, uint8_t len);

/**
 * @brief Receives a buffer of data from UART.
 *
 * @param[out] buf Pointer to the data buffer which stores the data received.
 * @param[in] len Number of bytes to receive.
 * @return int32_t Number of bytes actually received.
 */
int32_t sl_uart_drv_get_buf(uint8_t* buf, uint8_t len);

/**
 * @brief Receives a single character from UART.
 *
 * @param[out] ch Pointer to variable to store the received byte.
 * @return int32_t 1 if a byte was received, -1 if buffer is empty.
 */
int32_t sl_uart_drv_get_char(uint8_t *ch);

/**
 * @brief Get the number of bytes currently in the UART receive buffer.
 *
 * @return int32_t Number of bytes available in the receive buffer.
 */
int32_t sl_uart_rx_buf_count(void);

/**
 * @brief Get the status of the send completion flag.
 *
 * @return uint8_t Status of the send completion flag.
 */
uint8_t sl_get_send_complete_flag(void);

/**
 * @brief Get the status of the receive completion flag.
 *
 * @return uint8_t Status of the receive completion flag.
 */
uint8_t sl_get_receive_complete_flag(void);

/**
 * @brief Sends a single byte over UART (Serial API).
 *
 * @param[in] c Data byte to send.
 */
void sl_serial_api_drv_putc(uint8_t c);

/**
 * @brief Sends a buffer of data over UART (Serial API).
 *
 * @param[in] ptr Pointer to the data buffer.
 * @param[in] len Number of bytes to send.
 */
void sl_serial_api_drv_puts(const uint8_t *ptr, uint16_t len);

/**
 * @brief Initializes the Serial API UART driver.
 *
 * @return int 1 on success.
 */
int sl_serial_api_drv_init(void);

#define sl_uart_drv_put_char sl_serial_api_drv_putc
#define sl_uart_drv_send_buf sl_serial_api_drv_puts
#define sl_uart_drv_flush()

#endif // SL_UART_DRV_H
