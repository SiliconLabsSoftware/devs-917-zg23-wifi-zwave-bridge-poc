/*******************************************************************************
 * @file  sl_discover.c
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
#include "lwip/apps/mdns.h"
#include "lwip/netif.h"

#include "sl_common_log.h"

// Function to initialize mDNS with multiple services
void mdns_multi_service_init(struct netif *netif)
{
  // Initialize mDNS responder
  mdns_resp_init();

  // Set the hostname for mDNS
  mdns_resp_add_netif(netif, "my-multi-service-device", 255);

  // Add the first service (e.g., HTTP)
  mdns_resp_add_service(netif, "http-service", "_http", DNSSD_PROTO_TCP, 80, 255, NULL, NULL);

  // Add the second service (e.g., custom service)
  mdns_resp_add_service(netif, "custom-service", "_custom", DNSSD_PROTO_UDP, 12345, 255, NULL, NULL);

  // Add the third service (e.g., MQTT)
  mdns_resp_add_service(netif, "mqtt-service", "_mqtt", DNSSD_PROTO_TCP, 1883, 255, NULL, NULL);

  LOG_PRINTF("mDNS initialized with multiple services.\n");
}

// Example usage in your main thread or initialization function
void sl_tcpthread_init(void)
{
  struct netif *netif = netif_default;   // Assuming the default netif is already set up

  // Initialize mDNS with multiple services
  mdns_multi_service_init(netif);

  // Continue with the rest of your TCP thread logic
  LOG_PRINTF("TCP thread initialized.\n");
}
