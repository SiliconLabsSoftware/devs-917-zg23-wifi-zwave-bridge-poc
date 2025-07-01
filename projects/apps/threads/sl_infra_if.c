/***************************************************************************/ /**
 * @file sl_infra_if.c
 * @brief Infrastructure interface.
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
#include "cmsis_os2.h"

#include "Common/sl_common_log.h"
#include "Common/sl_common_type.h"

#include "sl_infra_if.h"

#include "lwip/icmp6.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/ethip6.h"
#include <lwip/netif.h>
#include "lwip/if_api.h"

#define RA_BUF_SIZE 128

struct icmp6_ra_hdr {
  uint8_t type;
  uint8_t code;
  uint16_t checksum;
  uint8_t cur_hop_limit;
  uint8_t flags;
  uint16_t router_lifetime;
  uint32_t reachable_time;
  uint32_t retrans_timer;
} __attribute__((packed));

static int sli_infra_if_socket = -1;

static void sli_infra_if_fill_pio(uint8_t *buf, const ip6_addr_t *prefix, uint8_t prefix_len)
{
  buf[0] = 3;   // Type: Prefix Information
  buf[1] = 4;   // Length: 4 (in units of 8 octets)
  buf[2] = prefix_len;   // Prefix length
  buf[3] = 0xc0;   // Flags: On-link + Autonomous
  buf[4] = 0x00; buf[5] = 0x00; buf[6] = 0x0e; buf[7] = 0x10;   // Valid Lifetime (3600s)
  buf[8] = 0x00; buf[9] = 0x00; buf[10] = 0x0e; buf[11] = 0x10;   // Preferred Lifetime (3600s)
  memset(buf + 12, 0, 4);   // Reserved
  memcpy(buf + 16, prefix, 16);   // Prefix
}

static void sli_infra_if_fill_rio(uint8_t *buf, const ip6_addr_t *route, uint8_t prefix_len, uint8_t prf)
{
  buf[0] = 24;   // Type: Route Information
  buf[1] = 3;    // Length: 3 (24 bytes)
  buf[2] = prefix_len;   // Prefix length
  buf[3] = (uint8_t)((prf & 0x03) << 3);   // Route Preference
  buf[4] = 0x00; buf[5] = 0x00; buf[6] = 0x0e; buf[7] = 0x10;   // Route Lifetime (3600s)
  memcpy(buf + 8, route, 16);   // Route Prefix
}

void sli_infra_if_send_icmpv6_ra(const char *ifname)
{
#if SL_USE_LWIP_STACK
  int sock;
  struct sockaddr_in6 dst_addr;
  uint8_t ra_buf[RA_BUF_SIZE];
  struct icmp6_ra_hdr *ra = (struct icmp6_ra_hdr *)ra_buf;
  int offset = sizeof(struct icmp6_ra_hdr);

  // Create raw ICMPv6 socket
  sock = socket(AF_INET6, SOCK_RAW, IPPROTO_ICMPV6);
  if (sock < 0) {
    LOG_PRINTF("Failed to create ICMPv6 socket: %d\n", errno);
    return;
  }

  // Bind to interface
  unsigned int ifindex = if_nametoindex(ifname);

  // Prepare RA header
  memset(ra_buf, 0, RA_BUF_SIZE);
  ra->type = 134;   // ND_ROUTER_ADVERT
  ra->code = 0;
  ra->cur_hop_limit = 64;
  ra->flags = 0;
  ra->router_lifetime = htons(1800);
  ra->reachable_time = 0;
  ra->retrans_timer = 0;

  // Add PIO
  ip6_addr_t prefix;
  ip6addr_aton(SL_INFRA_IF_PIO_PREFIX, &prefix);   // Example prefix
  sli_infra_if_fill_pio(ra_buf + offset, &prefix, SL_INFRA_IF_PIO_PREFIX_LEN);
  offset += 32;

  // Add RIO
  ip6_addr_t route;
  ip6addr_aton(SL_INFRA_IF_RIO_PREFIX, &route);   // Example route
  sli_infra_if_fill_rio(ra_buf + offset, &route, SL_INFRA_IF_RIO_PREFIX_LEN, 0);   // Medium preference
  offset += 24;

  // Destination address (all-nodes multicast)
  memset(&dst_addr, 0, sizeof(dst_addr));
  dst_addr.sin6_family = AF_INET6;
  inet_pton(AF_INET6, SL_INFRA_IF_MULT_PREFIX, &dst_addr.sin6_addr);
  dst_addr.sin6_scope_id = ifindex;

  // Send RA
  ssize_t sent = sendto(sock, ra_buf, offset, 0, (struct sockaddr *)&dst_addr, sizeof(dst_addr));
  if (sent < 0) {
    LOG_PRINTF("Failed to send RA: %d\n", errno);
  } else {
    LOG_PRINTF("Sent RA (%d bytes) on %s\n", (int)sent, ifname);
  }

  close(sock);
#endif
}

static void sli_infra_if_task(void *argument)
{
  (void) argument;
  struct netif *netif = netif_default;
  if (netif) {
    char ifname[IFNAMSIZ] = { 0 };
    netif_index_to_name(netif_get_index(netif), ifname);
    while (1) {
      sli_infra_if_send_icmpv6_ra(ifname);
      osDelay(SL_INFRA_IF_RA_PERIOD_MS);
    }
  }
}

void sl_infra_if_init(void)
{
  // Create raw ICMPv6 socket
  static const osThreadAttr_t attr = {
    .name = "infra_if_task",
    .stack_size = 3072,
    .priority = osPriorityNormal,
  };
  osThreadNew(sli_infra_if_task, NULL, &attr);
}

void sl_infra_if_deinit(void)
{
  if (sli_infra_if_socket >= 0) {
    close(sli_infra_if_socket);
    sli_infra_if_socket = -1;
  }
}
