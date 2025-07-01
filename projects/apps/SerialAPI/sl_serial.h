/***************************************************************************/ /**
 * @file sl_serial.h
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

#ifndef CONHANDLE_H_
#define CONHANDLE_H_

#include <stdint.h>

/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/
/* return values for ConUpdate */
enum T_CON_TYPE {
  conIdle,          // returned if nothing special has happened
  conFrameReceived, // returned when a valid frame has been received
  conFrameSent,     // returned if frame was ACKed by the other end
  conFrameErr,      // returned if frame has error in Checksum
  conRxTimeout,     // returned if Rx timeout has happened
  conTxTimeout,     // returned if Tx timeout (waiting for ACK) ahs happened
  conTxErr,         // We got a NAK after sending
  conTxWait,        // We got a CAN while sending
};

const char *sl_serial_get_state_name(enum T_CON_TYPE t);

/* defines for accessing serial protocol data */
#define serFrameLen     (*serBuf)
#define serFrameType    (*(serBuf + 1))
#define serFrameCmd     (*(serBuf + 2))
#define serFrameDataPtr (serBuf + 3)

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/
/* serial buffer size */
#define SERBUF_MAX       0xff
#define FRAME_LENGTH_MIN 3
#define FRAME_LENGTH_MAX SERBUF_MAX

/* Start Of Frame */
#define F_SOF 0x01
/* Acknowledge successful frame reception */
#define F_ACK 0x06
/* Not Acknowledge successful frame reception - please retransmit... */
#define F_NAK 0x15
/* Frame received (from host) was dropped - waiting for ACK */
#define F_CAN 0x18

#define REQUEST  0x00
#define RESPONSE 0x01

/**
 * @brief Transmit a frame via serial port by adding SOF, Len, Type, Cmd and Checksum.
 *
 * Frame format: SOF-Len-Type-Cmd-DATA-Checksum
 *
 * @param[in] cmd  Command byte.
 * @param[in] type Frame type to send (Response or Request).
 * @param[in] Buf  Pointer to data buffer to send.
 * @param[in] len  Length of data buffer.
 * @return Nothing.
 */
void
sl_serial_tx_frame(
  uint8_t cmd,
  uint8_t type,
  uint8_t *Buf,
  uint8_t len);

/**
 * @brief Parse serial data sent from external controller module (PC-controller).
 *        Should be called frequently in main loop.
 *
 * @param[in] acknowledge If non-zero, send ACK/NAK as appropriate.
 * @return enum T_CON_TYPE Connection state after processing.
 */
enum T_CON_TYPE
sl_serial_rx_frame(
  uint8_t acknowledge);

/**
 * @brief Initialize the serial module.
 *
 * @param[in] serial_port Serial port name (can be unused).
 * @return int Return code from UART driver initialization.
 */
int sl_serial_init(const char *serial_port);

/**
 * @brief Deinitialize the serial module and UART driver.
 *
 * @return void
 */
void sl_serial_destroy(void);

/**
 * @brief Copy internal serial buffer to user buffer.
 *
 * @param[out] buf_out Pointer to output buffer.
 * @return uint32_t Number of bytes copied.
 */
uint32_t sl_serial_get_buf(uint8_t *buf_out);

/**
 * @brief Set internal serial buffer from source buffer.
 *
 * @param[in] src_buf Pointer to source buffer.
 * @param[in] len     Length of source buffer.
 * @return void
 */
void sl_serial_set_local_buf(uint8_t *src_buf, uint16_t len);

/**
 * @brief Get length of internal serial buffer.
 *
 * @return uint8_t Length of the buffer.
 */
uint8_t sl_serial_get_local_buf_len(void);

/**
 * @brief Get pointer to internal serial buffer.
 *
 * @return uint8_t* Pointer to internal buffer.
 */
uint8_t *sl_serial_get_local_buf_data(void);

/**
 * @brief Read a single byte from the UART with a timeout.
 *
 * @param[out] ch Pointer to the variable where the received byte will be stored.
 * @return int 1 if a byte was read successfully, -1 if timeout occurred.
 */
int sl_serial_read_byte_block(uint8_t *ch);

/**
 * @brief Read a buffer of bytes from the UART with a timeout.
 *
 * @param[out] buf   Pointer to the buffer where received data will be stored.
 * @param[in]  rlen  Number of bytes to read.
 * @return int Number of bytes actually read. Can be less than @p rlen if timeout occurs.
 */
int sl_serial_read_buf_block(uint8_t *buf, uint32_t rlen);

/**
 * @brief Get string name for a given connection state.
 *
 * @param[in] t Connection state.
 * @return const char* Name of the state.
 */
const char *sl_serial_get_state_name(enum T_CON_TYPE t);

#endif /* CONHANDLE_H_ */
