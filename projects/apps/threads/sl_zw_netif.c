/***************************************************************************/ /**
 * @file sl_ze_netif.c
 * @brief Communication between z-wave nodes and external network.
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
#include "Common/sl_gw_info.h"

#include "sl_status.h"
#include "sl_zw_netif.h"
#include "sl_infra_if.h"

#include "Net/sl_udp_utils.h"

#include "lwip/icmp6.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/netif.h"
#include "lwip/ethip6.h"
#include "lwip/if_api.h"

#include "sl_tcpip_handler.h"

#define INTERFACE_ZW_NAME0 'z' ///< ZW Network interface name 0
#define INTERFACE_ZW_NAME1 'w' ///< ZW Network interface name 1

#define ZW_NETIF_NODE_MAX 40 ///< Maximum number of ZW nodes

#define SL_ZW_NETIF_MOCK_ENABLE 0

static struct netif zw_netif;
static sl_zw_netif_node_t zw_netif_nodes[ZW_NETIF_NODE_MAX];
NETIF_DECLARE_EXT_CALLBACK(sl_platform_netif_ext_callback);
static void sli_zw_netif_udp_listener(void *argument);

extern void print_heap_usage(void);

static void
sli_platform_netif_ext_callback_fn(struct netif *netif,
                                   netif_nsc_reason_t reason,
                                   const netif_ext_callback_args_t *args)
{
  LWIP_UNUSED_ARG(args);

  if (reason == LWIP_NSC_IPV6_ADDR_STATE_CHANGED) {
    char addrStr[46]       = { 0 };
    uint8_t idx            = args->ipv6_addr_state_changed.addr_index;
    const ip6_addr_t *addr = (ip6_addr_t *) &netif->ip6_addr[idx];

    /* Link local */
    if (idx == 0 && netif == netif_default) {
      LOG_PRINTF("IPv6 state changed \n");
    }

    if (ip6addr_ntoa_r(addr, addrStr, sizeof(addrStr)) != NULL) {
#if LWIP_IPV6_SCOPES
      LOG_PRINTF("Netif %c%c IP6[%d]: %s state %02x->%02x zone %d\n",
                 netif->name[0],
                 netif->name[1],
                 idx,
                 addrStr,
                 args->ipv6_addr_state_changed.old_state,
                 netif_ip6_addr_state(netif, idx),
                 addr->zone);
#else
      LOG_PRINTF("Netif %c%c IP6[%d]: %s state %02x->%02x\n",
                 netif->name[0],
                 netif->name[1],
                 idx,
                 addrStr,
                 args->ipv6_addr_state_changed.old_state,
                 netif_ip6_addr_state(netif, idx));
#endif
    }
  }
}

#if SL_ZW_NETIF_MOCK_ENABLE
static void sli_zw_netif_udp_callback(const struct sl_zw_netif_node *node,
                                      const uint8_t *data,
                                      size_t len)
{
  LOG_PRINTF("Callback: Received data from node id: %u, data length: %zu\n",
             node->id,
             len);
  LOG_PRINTF("Data: ");
  for (size_t i = 0; i < len; ++i) {
    LOG_PRINTF("%02X ", data[i]);
  }
  LOG_PRINTF("\n");
}
#endif

static int match_prefix(const struct in6_addr *addr, const char *prefix)
{
  uint32_t prefix_arr[4];
  inet_pton(AF_INET6, prefix, prefix_arr);
  const uint32_t *addr_arr = (const uint32_t *)addr;
  return (addr_arr[0] == prefix_arr[0] && addr_arr[1] == prefix_arr[1]);
}

static int find_node_by_ip6(const char *dststr)
{
  for (uint8_t i = 0; i < ZW_NETIF_NODE_MAX; i++) {
    if (zw_netif_nodes[i].in_use) {
      char node_addr_str[INET6_ADDRSTRLEN];
      if (inet_ntop(AF_INET6,
                    &zw_netif_nodes[i].ip6_addr,
                    node_addr_str,
                    sizeof(node_addr_str))
          && strcmp(node_addr_str, dststr) == 0) {
        return i;
      }
    }
  }
  return -1;
}

static void process_udp_packet(struct sockaddr_in6 *src_addr,
                               struct in6_pktinfo *pktinfo,
                               char *buf,
                               int len)
{
  char dststr[INET6_ADDRSTRLEN];
  if (!inet_ntop(AF_INET6, &pktinfo->ipi6_addr, dststr, sizeof(dststr))) {
    return;
  }
  int node_idx = find_node_by_ip6(dststr);
  if (node_idx >= 0) {
    sl_tcpip_buf_t *zippkt = sl_zip_packet_v6(&src_addr->sin6_addr,
                                              src_addr->sin6_port,
                                              dststr,
                                              (uint8_t *) buf,
                                              (uint16_t)len);
    if (zippkt) {
      zw_tcpip_post_event(1, zippkt);
    }
  }
}

static struct in6_pktinfo *extract_pktinfo(struct msghdr *msg)
{
  for (struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg); cmsg != NULL;
       cmsg = CMSG_NXTHDR(msg, cmsg)) {
    if (cmsg->cmsg_level == IPPROTO_IPV6
        && cmsg->cmsg_type == IPV6_PKTINFO) {
      return (struct in6_pktinfo *) CMSG_DATA(cmsg);
    }
  }
  return NULL;
}

static void sli_zw_netif_udp_listener(void *argument)
{
  (void) argument;

  int sock;
  struct sockaddr_in6 addr;

  sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    LOG_PRINTF("UDP socket create failed: %d\n", errno);
    osThreadExit();
  }

  memset(&addr, 0, sizeof(addr));
  addr.sin6_family = AF_INET6;
  addr.sin6_port   = htons(SL_ZW_NETIF_UDP_LISTENER_PORT); // Choose your port
  addr.sin6_addr   = in6addr_any; // Bind to all addresses
  bind(sock, (struct sockaddr *) &addr, sizeof(addr));

  // Enable receiving destination address info
  int on = 1;
  setsockopt(sock, IPPROTO_IPV6, IPV6_RECVPKTINFO, &on, sizeof(on));

  LOG_PRINTF("Listening for UDP packets to %s\\%d on port %d (IPv6)...\n",
             SL_INFRA_IF_RIO_PREFIX,
             SL_INFRA_IF_RIO_PREFIX_LEN,
             SL_ZW_NETIF_UDP_LISTENER_PORT);

  while (1) {
    struct sockaddr_in6 src_addr;
    struct iovec iov;
    struct msghdr msg;
    char buf[128];
    char cmsgbuf[CMSG_SPACE(sizeof(struct in6_pktinfo))];
    iov.iov_base = buf;
    iov.iov_len  = sizeof(buf);
    memset(&msg, 0, sizeof(msg));
    msg.msg_name       = &src_addr;
    msg.msg_namelen    = sizeof(src_addr);
    msg.msg_iov        = &iov;
    msg.msg_iovlen     = 1;
    msg.msg_control    = cmsgbuf;
    msg.msg_controllen = sizeof(cmsgbuf);

    int len = recvmsg(sock, &msg, 0);
    if (len > 0) {
      struct in6_pktinfo *pktinfo = extract_pktinfo(&msg);
      LOG_PRINTF("pktinfo: %p\n", pktinfo);
      if (pktinfo && match_prefix(&pktinfo->ipi6_addr, SL_INFRA_IF_RIO_PREFIX)) {
        process_udp_packet(&src_addr, pktinfo, buf, len);
      }
    }
  }
}

static err_t zw_netif_output(struct netif *netif,
                             struct pbuf *p,
                             const ip6_addr_t *ipaddr)
{
  // Here you can implement forwarding, NAT, or custom logic
  // For now, just log or drop
  (void) netif; // Unused parameter
  (void) p;     // Unused parameter
  (void) ipaddr; // Unused parameter
  return ERR_OK;
}

static err_t zw_netif_low_level_init(struct netif *netif)
{
  netif->name[0] = INTERFACE_ZW_NAME0;
  netif->name[1] = INTERFACE_ZW_NAME1;

  netif->output_ip6 = zw_netif_output;
  netif->linkoutput = NULL;
  netif->mtu        = 1500;
  netif->hwaddr_len = 6;
  netif->flags      = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP
                      | NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6
                      | NETIF_FLAG_LINK_UP;

  netif->hwaddr[0] = 0x02;
  netif->hwaddr[1] = 0x03;
  netif->hwaddr[2] = 0x04;
  netif->hwaddr[3] = 0x05;
  netif->hwaddr[4] = 0x06;
  netif->hwaddr[5] = 0x07;
  netif_set_link_up(netif);
  return ERR_OK;
}

static void zw_netif_setup(void)
{
  netif_add(&zw_netif,
            NULL,
            NULL,
            NULL,
            &zw_netif,
            zw_netif_low_level_init,
            NULL);
  netif_set_up(&zw_netif);
}

sl_zw_netif_node_t *sl_zw_netif_create_node(nodeid_t id)
{
  sl_zw_netif_node_t *node = NULL;
  for (uint8_t idx = 0; idx < ZW_NETIF_NODE_MAX; idx++) {
    if (zw_netif_nodes[idx].in_use == false) {
      node = &zw_netif_nodes[idx];
    }
  }
  if (!node) {
    LOG_PRINTF("Failed to create virtual zw node\n");
    return NULL;
  }
  memset(node, 0, sizeof(sl_zw_netif_node_t));
  node->id     = id;
  node->in_use = true;
  return node;
}

sl_status_t sl_zw_netif_remove_node(sl_zw_netif_node_t *node)
{
  if (node == NULL) {
    return SL_STATUS_FAIL;
  }
  memset(node, 0, sizeof(sl_zw_netif_node_t));
  return SL_STATUS_OK;
}

sl_status_t sl_zw_netif_add_ip6_address(sl_zw_netif_node_t *node)
{
  if (node == NULL) {
    return SL_STATUS_FAIL;
  }
  ip6_addr_t addr;
  err_t err;
  char addr_str[INET6_ADDRSTRLEN];
  sprintf(addr_str, "%s%x", SL_INFRA_IF_RIO_PREFIX, node->id);
  if (inet_pton(AF_INET6, addr_str, &addr) != 1) {
    LOG_PRINTF("Failed to convert IPv6 string to address: %s\n", addr_str);
    return SL_STATUS_FAIL;
  }
  err = netif_add_ip6_address(&zw_netif, &addr, (s8_t *) &node->addr_idx);
  if (err != ERR_OK) {
    LOG_PRINTF("Failed to add IPv6 address: %d\n", err);
    return SL_STATUS_FAIL;
  }
  netif_ip6_addr_set_state(&zw_netif,
                           (s8_t) node->addr_idx,
                           IP6_ADDR_PREFERRED);
  memcpy(&node->ip6_addr, &addr, sizeof(ip6_addr_t));
  return SL_STATUS_OK;
}

sl_status_t sl_zw_netif_remove_ip6_address(sl_zw_netif_node_t *node)
{
  if (node == NULL) {
    return SL_STATUS_FAIL;
  }
  netif_ip6_addr_set_state(&zw_netif, (s8_t) node->addr_idx, IP6_ADDR_INVALID);
  return SL_STATUS_OK;
}

sl_status_t sl_zw_netif_register_callback(sl_zw_netif_node_t *node,
                                          sl_zw_netif_recv_cb_t cb)
{
  if (node == NULL || cb == NULL) {
    return SL_STATUS_FAIL;
  }
  node->cb = cb;
  return SL_STATUS_OK;
}

sl_status_t sl_zw_netif_create_socket_udp(sl_zw_netif_node_t *node)
{
  if (node == NULL) {
    return SL_STATUS_FAIL;
  }
  if (node->sock >= 0) {
    LOG_PRINTF("Socket already created\n");
    return SL_STATUS_FAIL;
  }
  int sock = socket(AF_INET6, SOCK_DGRAM, 0);
  if (sock < 0) {
    LOG_PRINTF("UDP socket create failed: %d\n", errno);
    return SL_STATUS_FAIL;
  }
  // Bind the socket to the node's IPv6 address so that packets sent from this socket use it as the source address
  struct sockaddr_in6 bind_addr;
  memset(&bind_addr, 0, sizeof(bind_addr));
  bind_addr.sin6_family = AF_INET6;
  bind_addr.sin6_port   = 0; // Let the OS choose the port
  memcpy(&bind_addr.sin6_addr, &node->ip6_addr, sizeof(bind_addr.sin6_addr));
  if (bind(sock, (struct sockaddr *) &bind_addr, sizeof(bind_addr)) < 0) {
    LOG_PRINTF("UDP socket bind failed: %d\n", errno);
    close(sock);
    return SL_STATUS_FAIL;
  }
  node->sock = sock;
  return SL_STATUS_OK;
}

sl_status_t sl_zw_netif_socket_send(sl_zw_netif_node_t *node,
                                    const char *buf,
                                    size_t len,
                                    const char *ip6_str,
                                    uint16_t port)
{
  if (node == NULL || buf == NULL || node->sock <= 0) {
    return SL_STATUS_FAIL;
  }
  struct sockaddr_in6 dest_addr;
  memset(&dest_addr, 0, sizeof(dest_addr));
  dest_addr.sin6_family = AF_INET6;
  dest_addr.sin6_port   = htons(port);
  inet_pton(AF_INET6, ip6_str, &dest_addr.sin6_addr);

  ssize_t sent = sendto(node->sock,
                        buf,
                        len,
                        0,
                        (struct sockaddr *) &dest_addr,
                        sizeof(dest_addr));
  if (sent < 0) {
    LOG_PRINTF("UDP socket send failed: %d\n", errno);
    return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

void sl_zw_netif_init(void)
{
  netif_add_ext_callback(&sl_platform_netif_ext_callback,
                         sli_platform_netif_ext_callback_fn);
  zw_netif_setup();

  static const osThreadAttr_t zw_netif_udp_attr = {
    .name       = "zw_netif_udpd",
    .stack_size = 3072,
    .priority   = osPriorityNormal,
  };
  osThreadNew(sli_zw_netif_udp_listener, NULL, &zw_netif_udp_attr);

#if SL_ZW_NETIF_MOCK_ENABLE
  sl_zw_netif_node_t *node_1 = sl_zw_netif_create_node(6);
  sl_zw_netif_node_t *node_2 = sl_zw_netif_create_node(9);
  sl_zw_netif_add_ip6_address(node_1);
  sl_zw_netif_add_ip6_address(node_2);
  sl_zw_netif_register_callback(node_1, sli_zw_netif_udp_callback);
  sl_zw_netif_register_callback(node_2, sli_zw_netif_udp_callback);
#endif
}
