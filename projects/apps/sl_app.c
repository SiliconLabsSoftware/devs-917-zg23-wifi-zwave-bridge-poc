/*******************************************************************************
 * @file  app.c
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
#include "sl_board_configuration.h"
#include "sl_wifi.h"
#include "sl_wifi_callback_framework.h"
#include "cmsis_os2.h"
#include "sl_utility.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "sl_common_log.h"
#include "sl_cli.h"
#include "rsi_common_apis.h"

#include "sl_gw_info.h"

#include "modules/sl_si917_net.h"
#include "modules/sl_si917_aes.h"
#include "modules/sl_hw_rng.h"
#include "modules/sl_mbedtls_thread_impl.h"
#include "modules/sl_psram.h"
#include "apps/SerialAPI/sl_uart_drv.h"
#include "apps/ip_translate/sl_discover.h"

#include "apps/threads/sl_serial_api_handler.h"
#include "apps/threads/sl_ts_thread.h"
#include "apps/threads/sl_zw_router.h"
#include "apps/threads/sl_tcpip_handler.h"
#include "apps/threads/sl_tls_client.h"
#include "apps/threads/sl_zw_netif.h"
#include "apps/threads/sl_infra_if.h"
#include "apps/ble_prov/sl_ble_app.h"
#include "apps/ble_prov/sl_wlan_app.h"
#include "apps/ble_prov/sl_nvm3_cfg.h"

const osThreadAttr_t sl_app_thread_attributes = {
  .name       = "app_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 3072,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

osMutexId_t sl_log_mutex;

void print_heap_usage(void)
{
  LOG_PRINTF("Free heap: %u bytes\n", (unsigned int)xPortGetFreeHeapSize());
  LOG_PRINTF("Minimum ever free heap: %u bytes\n", (unsigned int)xPortGetMinimumEverFreeHeapSize());
  LOG_PRINTF("Stack high water mark: %u\n", (unsigned int)uxTaskGetStackHighWaterMark(NULL));
}

void sl_app_thread(void *argument)
{
  (void) argument;
  SL_LOG_PRINT("Mini Z-Wave Gateway Start!\n");
  SL_LOG_PRINT("Version: %d.%d.%d\n", FW_VERSION_MAJOR,
               FW_VERSION_MINOR, FW_VERSION_PATCH);
  sl_wlan_init();
  // fix issue nvm call before wifi_init.
  sl_store_init();
  // Initialize the PSRAM
  sl_psram_init();
  // Initialize the BLE module
#if (SL_WLAN_MOCK_ENABLE == 0)
  sl_ble_init();
#endif

  sl_mbedtls_threading_mutex_init();
  // init aes hardware
  sl_si917_aes_init();
  sl_hw_hrng_init();

  sl_cli_init();
  sl_serial_api_init();

  // wait wlan connected.
  SL_LOG_PRINT("Waiting connect AP\n");
  while (sl_wlan_connect_status() != SL_WLAN_STATUS_CONNECTED) {
    osDelay(1000);
  }

  sl_infra_if_init();
  sl_zw_netif_init();

  sl_zw_udp_init();
  sl_tcpip_init();
  sl_tls_client_init();

  sl_router_init();

  print_heap_usage();

  while (1) {
    sl_router_process();
    osDelay(100);
  }
}

void app_init(void)
{
  printf("run application\n");
  sl_log_mutex = osMutexNew(NULL);
  if (sl_log_mutex == NULL) {
    printf("Failed to create printf_mutex\r\n");
    return;
  }

  if (!osThreadNew((osThreadFunc_t) sl_app_thread,
                   NULL,
                   &sl_app_thread_attributes)) {
    LOG_PRINTF("main thread start FAIL!\n");
  }
}
