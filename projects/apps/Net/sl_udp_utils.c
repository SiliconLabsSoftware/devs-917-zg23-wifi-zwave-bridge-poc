/*******************************************************************************
 * @file  sl_udp_utils.c
 * @brief UDP utilities for Z-Wave message handling
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
#include "sl_status.h"

#include "sl_board_configuration.h"
#include "cmsis_os2.h"
#include "sl_wifi.h"
#include "sl_net.h"

#include "Common/sl_uip_def.h"
#include "Common/sl_tcpip_def.h"
#include "Common/sl_common_log.h"
#include "Common/sl_gw_info.h"

#if SL_USE_LWIP_STACK
#include "lwip/udp.h"
#include "lwip/ip_addr.h"
#include "lwip/mem.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "sl_net_for_lwip.h"
#else
#include "socket.h"
#include "sl_si91x_socket_support.h"
#include "sl_si91x_socket_utility.h"
#include "sl_si91x_socket_constants.h"
#include "sl_si91x_socket.h"
#include "sl_si91x_socket.h"
#endif
#include "sl_utility.h"
#include <string.h>
#include "sl_si91x_driver.h"
#include "sl_net_wifi_types.h"

#include "Net/ZW_udp_server.h"
#include "utls/ipv6_utils.h"
#include "ip_translate/ipv46_if_handler.h"
#include "threads/sl_infra_if.h"

#include "sl_udp_utils.h"

// this queue store z-wave message.
static osMessageQueueId_t sli_zw_msg_queue;

void uint32_to_ip(uint32_t ip, char *bytes)
{
  bytes[0] = (ip >> 24) & 0xFF;
  bytes[1] = (ip >> 16) & 0xFF;
  bytes[2] = (ip >> 8) & 0xFF;
  bytes[3] = ip & 0xFF;
}

void sl_udp_store_zw_msg_init(void)
{
  sli_zw_msg_queue = osMessageQueueNew(2, sizeof(zw_msg_t), NULL);
}

int sl_udp_store_zw_msg_put(struct async_state *c,
                            uint8_t *data,
                            uint16_t len)
{
  zw_msg_t msg = { .conn     = c->conn,
                   .ack_req  = c->ack_req,
                   .data     = data,
                   .data_len = len };
  osMessageQueuePut(sli_zw_msg_queue, (void *) &msg, 0, 0);
  return len;
}

sl_status_t sl_udp_store_zw_msg_get(void *msg, uint32_t timeout)
{
  if (osMessageQueueGet(sli_zw_msg_queue, msg, NULL, timeout) == osOK) {
    return SL_STATUS_OK;
  }
  return SL_STATUS_TIMEOUT;
}

void sl_udp_ip46_to_sockaddr(uint32_t *s, uip_ip6addr_t *ipv64)
{
  LOG_PRINTF("ipv64: %x:%x\n", ipv64->u16[6], ipv64->u16[7]);
  *s = (ipv64->u16[6]) | (ipv64->u16[7] << 16);
}

int sl_udp_packet_send(struct uip_udp_conn *c, uint8_t *data, uint16_t len)
{
  // Create socket
  int client_socket                 = -1;
  struct sockaddr_in server_address = { 0 };
  socklen_t socket_length           = sizeof(struct sockaddr_in);
  int sent_bytes                    = 1;
  server_address.sin_family         = AF_INET;
  server_address.sin_port           = ZWAVE_PORT;

  client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (client_socket < 0) {
    LOG_PRINTF("\r\nSocket creation failed with bsd error: %d\r\n", errno);
    return SL_STATUS_FAIL;
  }
  LOG_PRINTF("\r\nSocket ID : %d\r\n", client_socket);
  sent_bytes = sendto(client_socket,
                      data,
                      len,
                      0,
                      (struct sockaddr *) &server_address,
                      socket_length);
  if (sent_bytes < 0) {
    LOG_PRINTF("\r\nSocket send failed with bsd error: %d\r\n", errno);
    close(client_socket);
    return SL_STATUS_FAIL;
  }

  LOG_PRINTF("\r\nBytes sent : %d ", sent_bytes);
  LOG_PRINTF(" to: ");
  uip_debug_ipaddr_print(&c->ripaddr);
  LOG_PRINTF(":%d\n", server_address.sin_port)
  close(client_socket);
  return len;
}

int sl_udp_packet_send_v6(struct uip_udp_conn *c, const uint8_t *data, uint16_t len)
{
  int client_socket = -1;
  struct sockaddr_in6 server_address = { 0 };
  socklen_t socket_length = sizeof(struct sockaddr_in6);
  int sent_bytes = 1;

  server_address.sin6_family = AF_INET6;
  server_address.sin6_port = (((uint16_t)ZWAVE_PORT & 0xFF) << 8) | (((uint16_t)ZWAVE_PORT >> 8) & 0xFF); //(ZWAVE_PORT);
  // Copy IPv6 address
  memcpy(&server_address.sin6_addr, c->ripaddr.u8, 16);

  client_socket = socket(AF_INET6, SOCK_DGRAM, IPPROTO_UDP);
  if (client_socket < 0) {
    LOG_PRINTF("\r\nSocket creation failed with bsd error: %d\r\n", errno);
    return SL_STATUS_FAIL;
  }
  LOG_PRINTF("\r\nSocket ID : %d\r\n", client_socket);
  sent_bytes = sendto(client_socket,
                      data,
                      len,
                      0,
                      (struct sockaddr *) &server_address,
                      socket_length);
  if (sent_bytes < 0) {
    LOG_PRINTF("\r\nSocket send failed with bsd error: %d\r\n", errno);
    close(client_socket);
    return SL_STATUS_FAIL;
  }

  LOG_PRINTF("\r\nBytes sent : %d ", sent_bytes);
  LOG_PRINTF(" to: ");
  uip_debug_ipaddr_print(&c->ripaddr);
  LOG_PRINTF(" port %d\n", server_address.sin6_port);
  close(client_socket);
  return len;
}

sl_tcpip_buf_t* sl_zip_packet_v6(struct in6_addr *in,
                                 uint16_t in_port,
                                 char *lip,
                                 uint8_t *buf,
                                 uint16_t len)
{
  struct uip_udp_conn conn;
  conn.lport = ZWAVE_PORT;
  memcpy(conn.ripaddr.u8, in->un.u8_addr, 16);

  LOG_PRINTF("\nRecv UDPv6 data: %d bytes from ", len);
  uip_debug_ipaddr_print((const uip_ipaddr_t *) &conn.ripaddr);
  LOG_PRINTF(" port %d, TIME: %ld\n", in_port, osKernelGetTickCount());

  conn.rport = in_port;

  // forward to tcpip.
  sl_tcpip_buf_t *tcpip_buf = (sl_tcpip_buf_t *) malloc(sizeof(sl_tcpip_buf_t));
  if (tcpip_buf == NULL) {
    ERR_PRINTF("malloc tcpip_buf failed\n");
    return NULL;
  }

  uint8_t *zip_data = (uint8_t *) malloc(len);
  if (zip_data == NULL) {
    ERR_PRINTF("malloc zip_data failed\n");
    free(tcpip_buf);
    return NULL;
  }
  memcpy(zip_data, buf, len);
  tcpip_buf->zw_con.ripaddr = conn.ripaddr;
  sl_uiplib_ipaddrconv((const char *) lip, &(tcpip_buf->zw_con.lipaddr));
  tcpip_buf->zw_con.rport     = conn.rport;
  tcpip_buf->zw_con.lport     = ZWAVE_PORT;
  tcpip_buf->tcpip_proto      = UIP_PROTO_UDP;
  tcpip_buf->zip_data         = (char *) zip_data;
  tcpip_buf->zip_data_len     = len;
  tcpip_buf->zip_packet       = (ZW_COMMAND_ZIP_PACKET *) tcpip_buf->zip_data;
  tcpip_buf->zw_con.lendpoint = tcpip_buf->zip_packet->sEndpoint;
  tcpip_buf->zw_con.rendpoint = tcpip_buf->zip_packet->dEndpoint;
  tcpip_buf->zw_con.rx_flags  = tcpip_buf->zip_packet->flags0;
  tcpip_buf->zw_con.tx_flags  = tcpip_buf->zip_packet->flags1;

  return tcpip_buf;
}

sl_tcpip_buf_t* sl_zip_packet_v4(struct sockaddr_in *in, uint8_t *buf, uint16_t len)
{
  struct uip_udp_conn conn;
  struct sockaddr_in *ip = in;

  uint8_t aip[4];
  char ip_str[64] = { 0 };
  nodeid_t nodeid = 0;
  uint32_to_ip(ip->sin_addr.s_addr, (char *) aip);

  LOG_PRINTF("\nRecv UDP data: %d bytes ", len);
  LOG_PRINTF(" from: %d.%d.%d.%d:%d, s_addr: %ld\n",
             aip[3],
             aip[2],
             aip[1],
             aip[0],
             ip->sin_port,
             ip->sin_addr.s_addr);

  conn.lport = ZWAVE_PORT;
  uip_ip4addr_t ipv4;
  memcpy(&ipv4, &ip->sin_addr.s_addr, 4);
  ip4to6_addr(&conn.ripaddr, &ipv4);
  conn.rport = ip->sin_port;

  // forward to tcpip. In this version, we are including dest nodeid in UDP message.
  sl_tcpip_buf_t *tcpip_buf = (sl_tcpip_buf_t *) malloc(sizeof(sl_tcpip_buf_t));
  if (tcpip_buf == NULL) {
    ERR_PRINTF("malloc tcpip_buf failed\n");
    return NULL;
  }

  uint8_t *zip_data = (uint8_t *) malloc(len);
  if (zip_data == NULL) {
    ERR_PRINTF("malloc zip_data failed\n");
    free(tcpip_buf);
    return NULL;
  }
  memcpy(zip_data, buf + 2, len - 2);
  tcpip_buf->zw_con.ripaddr = conn.ripaddr;
  nodeid                    = (nodeid_t)((buf[0]) | (buf[1] << 8));
  snprintf(ip_str, sizeof(ip_str), "%s%x", SL_INFRA_IF_RIO_PREFIX, nodeid);
  sl_uiplib_ipaddrconv((const char *) ip_str, &(tcpip_buf->zw_con.lipaddr));
  tcpip_buf->zw_con.rport = conn.rport;
  tcpip_buf->zw_con.lport = ZWAVE_PORT;
  tcpip_buf->tcpip_proto  = UIP_PROTO_UDP;
  tcpip_buf->zip_data     = (char *) zip_data;
  tcpip_buf->zip_data_len = len - 2;
  tcpip_buf->zip_packet   = (ZW_COMMAND_ZIP_PACKET *) tcpip_buf->zip_data;
  tcpip_buf->zw_con.lendpoint = tcpip_buf->zip_packet->sEndpoint;
  tcpip_buf->zw_con.rendpoint = tcpip_buf->zip_packet->dEndpoint;
  tcpip_buf->zw_con.rx_flags  = tcpip_buf->zip_packet->flags0;
  tcpip_buf->zw_con.tx_flags  = tcpip_buf->zip_packet->flags1;

  return tcpip_buf;
}
