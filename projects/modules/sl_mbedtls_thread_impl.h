/*******************************************************************************
 * @file  sl_mbedtls_thread_impl.h
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

#ifndef MODULES_SL_MBEDTLS_THREAD_IMPL_H_
#define MODULES_SL_MBEDTLS_THREAD_IMPL_H_

/**
 * @brief Initialize a mbedTLS mutex using CMSIS-RTOS2.
 *
 * This function creates a new CMSIS-RTOS2 mutex and assigns it to the
 * mbedtls_threading_mutex_t structure for use by mbedTLS.
 *
 * @param mutex Pointer to the mbedTLS mutex structure to initialize.
 */
void sl_mbedtls_threading_mutex_init(void);

#endif /* MODULES_SL_MBEDTLS_THREAD_IMPL_H_ */
