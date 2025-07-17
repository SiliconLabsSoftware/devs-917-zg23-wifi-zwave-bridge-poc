/*******************************************************************************
 * @file  sl_hw_rng.c
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

#include "sli_mbedtls_config_all.h"

#ifdef MBEDTLS_ENTROPY_HARDWARE_ALT

#include "string.h"
#include "entropy_poll.h"
#include "sl_si91x_hrng.h"
#include "sl_status.h"
#include "sl_common_log.h"

/**
 * @brief Initialize the hardware random number generator (HRNG).
 *
 * This function initializes the Silicon Labs SI91x hardware RNG module.
 * It must be called before using the hardware RNG for entropy generation.
 *
 * @return SL_STATUS_OK on success, error code otherwise.
 */
sl_status_t sl_hw_hrng_init(void)
{
  sl_status_t status;
  status = sl_si91x_hrng_init();
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to start HRNG\n");
  }
  return status;
}

/**
 * @brief mbedTLS hardware entropy poll callback.
 *
 * This function provides entropy to mbedTLS using the SI91x hardware RNG.
 * It alternates between true random and pseudo-random modes for each call.
 * The function fills the output buffer with random bytes and updates the output length.
 * It is intended to be registered as the hardware entropy source for mbedTLS.
 *
 * @param Data   Unused context pointer (required by mbedTLS API).
 * @param Output Buffer to fill with random bytes.
 * @param Len    Number of random bytes requested.
 * @param oLen   Pointer to variable to store the number of bytes actually written.
 *
 * @return 0 on success, error code otherwise.
 */
int mbedtls_hardware_poll(void *Data, unsigned char *Output, size_t Len, size_t *oLen)
{
  (void)Data; // Đánh dấu tham số không sử dụng để tránh lỗi biên dịch
  sl_status_t status;
  uint32_t index;
  uint32_t randomValue;
  static sl_si91x_hrng_mode_t hrng_mode = SL_SI91X_HRNG_TRUE_RANDOM;

  status = sl_si91x_hrng_start(hrng_mode);
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to start HRNG\n");
    return status;
  } else {
    if (hrng_mode == SL_SI91X_HRNG_TRUE_RANDOM) {
      hrng_mode = SL_SI91X_HRNG_PSEUDO_RANDOM;
    } else {
      hrng_mode = SL_SI91X_HRNG_TRUE_RANDOM;
    }
  }

  for (index = 0; index < Len / 4; index++) {
    if (sl_si91x_hrng_get_bytes(&randomValue, 1) == SL_STATUS_OK) {
      *oLen += 4;
      memset(&(Output[index * 4]), (int)randomValue, 4);
    } else {
      LOG_PRINTF("mbedtls_hardware_poll: error!\n");
      break;
    }
  }
  /* Stop the HRNG */
  status = sl_si91x_hrng_stop();
  if (status != SL_STATUS_OK) {
    LOG_PRINTF("Failed to Stop HRNG\n");
    return status;
  }

  return 0;
}

#endif /*MBEDTLS_ENTROPY_HARDWARE_ALT*/
