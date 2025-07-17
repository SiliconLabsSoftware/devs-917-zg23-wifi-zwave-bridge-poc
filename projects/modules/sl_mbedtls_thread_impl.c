/*******************************************************************************
 * @file  sl_mbedtls_thread_impl.c
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

#include "cmsis_os2.h"
#include "mbedtls/threading.h"

/**
 * @brief Initialize a mbedTLS mutex using CMSIS-RTOS2.
 *
 * This function creates a new CMSIS-RTOS2 mutex and assigns it to the
 * mbedtls_threading_mutex_t structure for use by mbedTLS.
 *
 * @param mutex Pointer to the mbedTLS mutex structure to initialize.
 */
void sl_mbedtls_mutex_init(mbedtls_threading_mutex_t *mutex)
{
  mutex->mutex_ID = osMutexNew(NULL);
}

/**
 * @brief Free a mbedTLS mutex created with CMSIS-RTOS2.
 *
 * This function deletes the CMSIS-RTOS2 mutex associated with the
 * mbedtls_threading_mutex_t structure.
 *
 * @param mutex Pointer to the mbedTLS mutex structure to free.
 */
void sl_mbedtls_mutex_free(mbedtls_threading_mutex_t *mutex)
{
  if (mutex->mutex_ID) {
    osMutexDelete(mutex->mutex_ID);
  }
}

/**
 * @brief Lock a mbedTLS mutex using CMSIS-RTOS2.
 *
 * This function acquires the CMSIS-RTOS2 mutex for use by mbedTLS.
 *
 * @param mutex Pointer to the mbedTLS mutex structure to lock.
 * @return 0 on success, MBEDTLS_ERR_THREADING_BAD_INPUT_DATA or MBEDTLS_ERR_THREADING_MUTEX_ERROR on failure.
 */
int sl_mbedtls_mutex_lock(mbedtls_threading_mutex_t *mutex)
{
  if (!mutex->mutex_ID) {
    return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
  }
  return osMutexAcquire(mutex->mutex_ID, osWaitForever) == osOK ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}

/**
 * @brief Unlock a mbedTLS mutex using CMSIS-RTOS2.
 *
 * This function releases the CMSIS-RTOS2 mutex for use by mbedTLS.
 *
 * @param mutex Pointer to the mbedTLS mutex structure to unlock.
 * @return 0 on success, MBEDTLS_ERR_THREADING_BAD_INPUT_DATA or MBEDTLS_ERR_THREADING_MUTEX_ERROR on failure.
 */
int sl_mbedtls_mutex_unlock(mbedtls_threading_mutex_t *mutex)
{
  if (!mutex->mutex_ID) {
    return MBEDTLS_ERR_THREADING_BAD_INPUT_DATA;
  }
  return osMutexRelease(mutex->mutex_ID) == osOK ? 0 : MBEDTLS_ERR_THREADING_MUTEX_ERROR;
}

/**
 * @brief Register the custom CMSIS-RTOS2 mutex functions with mbedTLS.
 *
 * This function sets the alternate threading implementation for mbedTLS
 * to use the CMSIS-RTOS2-based mutex functions defined above.
 */
void sl_mbedtls_threading_mutex_init(void)
{
  mbedtls_threading_set_alt(sl_mbedtls_mutex_init, sl_mbedtls_mutex_free, sl_mbedtls_mutex_lock, sl_mbedtls_mutex_unlock);
}
