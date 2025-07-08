/***************************************************************************/ /**
 * @file sl_zw_netif.h
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

#ifndef SL_ZW_NETIF_H
#define SL_ZW_NETIF_H

#include "Net/ZW_udp_server.h"
#include "Common/sl_rd_types.h"
#include "lwip/ip.h"

#define SL_ZW_NETIF_UDP_LISTENER_PORT ZWAVE_PORT

struct sl_zw_netif_node; // Forward declaration

typedef void (*sl_zw_netif_recv_cb_t)(const struct sl_zw_netif_node *node, const uint8_t *data, size_t len);

typedef struct sl_zw_netif_node {
  ip6_addr_t ip6_addr;          ///< IPv6 address
  int sock;                     ///< Socket for UDP communication
  nodeid_t id;                  ///< Node ID
  uint8_t addr_idx;             ///< Netif address index
  sl_zw_netif_recv_cb_t cb;     ///< Callback function for receiving data
  bool in_use;                  ///< Flag to indicate if the node is in use
} sl_zw_netif_node_t;

/**
 * @brief Initializes the Z-Wave network interface.
 */
void sl_zw_netif_init(void);

/**
 * @brief Creates a new virtual Z-Wave network node with the specified node ID.
 *
 * @param id The node ID to assign to the new node.
 * @return Pointer to the created sl_zw_netif_node_t structure, or NULL on failure.
 */
sl_zw_netif_node_t* sl_zw_netif_create_node(nodeid_t id);

/**
 * @brief Removes a virtual Z-Wave network node.
 *
 * @param node Pointer to the node to remove.
 * @return SL_STATUS_OK if the node was successfully removed, SL_STATUS_FAIL otherwise.
 */
sl_status_t sl_zw_netif_remove_node(sl_zw_netif_node_t* node);

/**
 * @brief Adds an IPv6 address to the specified Z-Wave network node.
 *
 * @param node Pointer to the node to which the IPv6 address will be added.
 * @return SL_STATUS_OK if the address was successfully added, SL_STATUS_FAIL otherwise.
 */
sl_status_t sl_zw_netif_add_ip6_address(sl_zw_netif_node_t* node);

/**
 * @brief Removes the IPv6 address from the specified Z-Wave network node.
 *
 * @param node Pointer to the node from which the IPv6 address will be removed.
 * @return SL_STATUS_OK if the address was successfully removed, SL_STATUS_FAIL otherwise.
 */
sl_status_t sl_zw_netif_remove_ip6_address(sl_zw_netif_node_t* node);

/**
 * @brief Registers a callback function for receiving data on the specified node.
 *
 * @param node Pointer to the node for which the callback is registered.
 * @param cb Callback function to be registered.
 * @return SL_STATUS_OK if the callback was successfully registered, SL_STATUS_FAIL otherwise.
 */
sl_status_t sl_zw_netif_register_callback(sl_zw_netif_node_t* node, sl_zw_netif_recv_cb_t cb);

/**
 * @brief Creates a UDP socket for the specified Z-Wave network node.
 *
 * @param node Pointer to the node for which the UDP socket will be created.
 * @return SL_STATUS_OK if the socket was successfully created, SL_STATUS_FAIL otherwise.
 */
sl_status_t sl_zw_netif_create_socket_udp(sl_zw_netif_node_t* node);

/**
 * @brief Sends data over a UDP socket from the specified node.
 *
 * @param node Pointer to the node sending the data.
 * @param buf Pointer to the buffer containing the data to send.
 * @param len Length of the data to send.
 * @param ip6_str String representation of the destination IPv6 address.
 * @param port Destination port number.
 * @return SL_STATUS_OK if the data was successfully sent, SL_STATUS_FAIL otherwise.
 */
sl_status_t sl_zw_netif_socket_send(sl_zw_netif_node_t* node, const char *buf, size_t len, const char *ip6_str, uint16_t port);

#endif // SL_ZW_NETIF_H
