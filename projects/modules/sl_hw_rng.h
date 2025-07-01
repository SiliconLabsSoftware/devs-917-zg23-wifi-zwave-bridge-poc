/*******************************************************************************
 * @file  sl_hw_rng.h
 * @brief aes functions
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
#ifndef SL_HW_RNG_H
#define SL_HW_RNG_H

#include "sl_status.h"

/**
 * @brief Initialize the hardware random number generator (HRNG).
 *
 * This function initializes the Silicon Labs SI91x hardware RNG module.
 * It must be called before using the hardware RNG for entropy generation.
 *
 * @return SL_STATUS_OK on success, error code otherwise.
 */
sl_status_t sl_hw_hrng_init(void);

#endif /* SL_HW_RNG_H */
