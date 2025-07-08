/***************************************************************************/ /**
 * @file sl_ts_thread.c
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

#include "FreeRTOS.h"
#include "Common/sl_common_log.h"

#include "Z-Wave/include/ZW_classcmd.h"

#include "transport/sl_zw_frm.h"
#include "transport/sl_zw_send_data.h"
#include "transport/sl_zw_send_request.h"

void sl_ts_init(void)
{
  LOG_PRINTF("ts thread start\n");
  sl_zw_send_data_appl_init();
  sl_zw_send_request_init();
}
