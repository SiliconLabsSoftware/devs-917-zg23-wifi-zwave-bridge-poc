/*******************************************************************************
 * @file  sl_psram.c
 * @brief PSRAM module
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

#include <modules/sl_psram.h>
#include "rsi_board.h"
#include "sl_common_log.h"
#include "sl_si91x_psram_handle.h"

/****************************************************************************/
/*                            LOCAL VARIABLES                               */
/****************************************************************************/

/****************************************************************************/
/*                            PUBLIC FUNCTIONS                              */
/****************************************************************************/

sl_psram_return_type_t sl_psram_init()
{
  sl_psram_return_type_t status;

  SystemCoreClockUpdate();
  RSI_Board_Init();

  // Initialize PSRAM
  status = sl_si91x_psram_uninit();
  if (status != PSRAM_SUCCESS) {
    ERR_PRINTF("Un-initialize PSRAM error status: %d\r\n", status);
    return status;
  }

  status = sl_si91x_psram_init();
  if (status != PSRAM_SUCCESS) {
    ERR_PRINTF("Initialize PSRAM error status: %d\r\n", status);
    return status;
  }

  return status;
}

void sl_psram_write_auto_mode(uint32_t addr,
                              uint8_t* SourceBuf,
                              uint32_t num_of_elements)
{
  uint8_t* psram_buf_wrt_pointer = (uint8_t*) addr;

  for (uint32_t index = 0; index < num_of_elements; index++) {
    psram_buf_wrt_pointer[index] = SourceBuf[index];
  }
}

void sl_psram_read_auto_mode(uint32_t addr,
                             uint8_t* DestBuf,
                             uint32_t num_of_elements)
{
  uint8_t* psram_buf_read_pointer = (uint8_t*) addr;

  for (uint32_t index = 0; index < num_of_elements; index++) {
    DestBuf[index] = psram_buf_read_pointer[index];
  }
}
