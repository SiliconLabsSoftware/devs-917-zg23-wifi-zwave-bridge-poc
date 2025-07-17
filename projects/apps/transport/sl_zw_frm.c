/***************************************************************************/ /**
 * @file sl_zw_frm.c
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

#include <stdlib.h>
#include <assert.h>
#include <string.h>
#include "sl_zw_frm.h"

zw_frame_buffer_element_t *zw_frame_buffer_alloc()
{
  return malloc(sizeof(zw_frame_buffer_element_t));
}

zw_frame_buffer_element_t *zw_frame_buffer_create(const ts_param_t *p,
                                                  const uint8_t *cmd,
                                                  uint16_t length)
{
  zw_frame_buffer_element_t *f = zw_frame_buffer_alloc();

  if (f && (length < sizeof(f->frame_data))) {
    f->param     = *p;
    f->frame_len = length;
    memcpy(f->frame_data, cmd, length);
    return f;
  } else {
    zw_frame_buffer_free(f);
    return NULL;
  }
}

void zw_frame_buffer_free(zw_frame_buffer_element_t *e)
{
  if (e) {
    free(e);
  }
}
