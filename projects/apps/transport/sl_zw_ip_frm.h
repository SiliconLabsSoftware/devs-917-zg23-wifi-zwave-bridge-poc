/***************************************************************************/ /**
 * @file sl_zw_ip_frm.h
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

#ifndef ZW_FRAME_BUFFER_H
#define ZW_FRAME_BUFFER_H
#include <stdint.h>
#include "ZW_udp_server.h"
/**
 * This module contains a structure for allocating Z-Wave frames buffers.
 * A Z-Wave frame buffer is defined as a Z-Wave application frame, and
 * a Z-Wave
 *
 */

#define FRAME_BUFFER_ELEMENT_SIE 256

typedef struct {
  zwave_connection_t conn;
  uint8_t frame_data[FRAME_BUFFER_ELEMENT_SIE];
  uint16_t frame_len;
} zw_frame_ip_buffer_element_t;

/**
 * Allocate a new frame IP buffer, the buffer must be freed with
 * \ref zw_frame_buffer_free
 */
zw_frame_ip_buffer_element_t *zw_frame_ip_buffer_alloc();

/**
 * Allocate and initialize a new  IP frame buffer, the buffer must be freed with
 * \ref zw_frame_buffer_free.
 */
zw_frame_ip_buffer_element_t *
zw_frame_ip_buffer_create(const zwave_connection_t *p,
                          const uint8_t *cmd,
                          uint16_t length);

/**
 * Free a frame IP buffer
 */
void zw_frame_ip_buffer_free(zw_frame_ip_buffer_element_t *e);

#endif
