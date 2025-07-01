/*******************************************************************************
 * @file  sl_psram.h
 * @brief Header file for PSRAM module functions
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
#ifndef MODULES_SL_PSRAM_H_
#define MODULES_SL_PSRAM_H_

#include "sl_si91x_psram.h"

/** partition design
 * name                 | addr              | length
 * physical PSRAM base  | 0x0               | 0x1F4 000 // 2MB
 * Controller image     | 0x1F4000          | 0xFA 000 // 1MB
 * Node image           | 0x2EE000          | 0x40 000 // 256KB
 *
 **/
#define NCP_IMG_PSRAM_BASE_ADDRESS 0x1F4000
#define NODE_IMG_PSRAM_BASE_ADDRESS 0x2EE000
#define PSRAM_CONTROLLER_IMG_BASE_ADDRESS (PSRAM_BASE_ADDRESS + NCP_IMG_PSRAM_BASE_ADDRESS)
#define PSRAM_NODE_IMG_BASE_ADDRESS       (PSRAM_BASE_ADDRESS + NODE_IMG_PSRAM_BASE_ADDRESS)

/**
 * @brief Initialize the PSRAM module.
 *
 * @return
 *   Status code indicating the result.
 *   - PSRAM_SUCCESS: Initialization successful
 *   - PSRAM_FAILURE: Initialization failed
 */

sl_psram_return_type_t sl_psram_init();

/**
 * @brief To write data to PSRAM in auto mode using DMA.
 * @param[in] addr
 *   PSRAM address for the write operation.
 * @param[in] SourceBuf
 *   Reference to the source buffer.
 * @param[in] num_of_elements
 *   Number of elements for the write operation.
 */

void sl_psram_write_auto_mode(uint32_t addr,
                              uint8_t* SourceBuf,
                              uint32_t num_of_elements);

/**
 * @brief To read data from PSRAM in manual mode using DMA.
 * @param[in] addr
 *   PSRAM address for the read operation.
 * @param[out] DestBuf
 *   Reference to the destination buffer.
 * @param[in] num_of_elements
 *   Number of elements for the read operation.
 */

void sl_psram_read_auto_mode(uint32_t addr,
                             uint8_t* DestBuf,
                             uint32_t num_of_elements);

#endif /* MODULES_SL_PSRAM_H_ */
