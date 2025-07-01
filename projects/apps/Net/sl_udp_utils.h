/***************************************************************************/ /**
 * @file sl_udp_utils.h
 * @brief UDP utilities for Z-Wave applications
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

#ifndef APPS_NET_SL_UDP_PORT_H_
#define APPS_NET_SL_UDP_PORT_H_
#include <stdint.h>
#include "sl_uip_def.h"
#include "sl_tcpip_def.h"
#include "ZW_udp_server.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"

/**
 * @brief Metadata structure for asynchronous UDP packets
 *
 * This structure saves metadata for the UDP packet received and saved along
 * with UDP payload (Z-Wave packet without ZIP header) by
 * uip_packetqueue_alloc() in the async_queue.
 */
struct async_state {
  uint8_t ack_req;             /**< Flag indicating if an acknowledgement is required */
  zwave_connection_t conn;     /**< Z-Wave connection information */
  //ZW_APPLICATION_META_TX_BUFFER rxBuf;
};

/**
 * @brief Z-Wave message container structure
 *
 * Structure used to store and pass Z-Wave messages between components,
 * including connection information and payload data.
 */
typedef struct {
  uint8_t ack_req;             /**< Flag indicating if an acknowledgement is required */
  zwave_connection_t conn;     /**< Z-Wave connection information */
  void *data;                  /**< Pointer to the message payload data */
  uint16_t data_len;           /**< Length of the message payload in bytes */
} zw_msg_t;

/**
 * @brief Store a Z-Wave message in the message queue
 *
 * Places a Z-Wave message with connection state and data into the message queue
 * for later processing.
 *
 * @param[in] c Connection state information
 * @param[in] data Pointer to the message data
 * @param[in] len Length of the message data in bytes
 * @return Length of data stored, or negative value on error
 */
int sl_udp_store_zw_msg_put(struct async_state *c,
                            uint8_t *data,
                            uint16_t len);

/**
 * @brief Retrieve a Z-Wave message from the message queue
 *
 * Gets a message from the Z-Wave message queue with optional timeout.
 *
 * @param[out] msg Pointer to a buffer that will receive the message
 * @param[in] timeout Maximum time to wait in milliseconds (0 for no wait)
 * @return SL_STATUS_OK on success, SL_STATUS_TIMEOUT if no message is available within timeout
 */
sl_status_t sl_udp_store_zw_msg_get(void *msg, uint32_t timeout);

/**
 * @brief Initialize the Z-Wave message queue
 *
 * Creates and initializes the message queue used for Z-Wave message storage.
 */
void sl_udp_store_zw_msg_init(void);

/**
 * @brief Send a UDP packet
 *
 * Sends a UDP packet over the specified connection.
 *
 * @param[in] c Pointer to the UDP connection structure
 * @param[in] data Pointer to the data to be sent
 * @param[in] len Length of the data in bytes
 * @return Number of bytes sent, or negative value on error
 */
/**
 * @brief Send a UDP packet
 *
 * Sends a UDP packet over the specified connection.
 *
 * @param[in] c Pointer to the UDP connection structure
 * @param[in] data Pointer to the data to be sent
 * @param[in] len Length of the data in bytes
 * @return Number of bytes sent, or negative value on error
 */
int sl_udp_packet_send(struct uip_udp_conn *c, uint8_t *data, uint16_t len);

/**
 * @brief Send a UDP packet over IPv6
 *
 * Sends a UDP packet over the specified IPv6 connection.
 *
 * @param[in] c Pointer to the UDP connection structure with IPv6 address
 * @param[in] data Pointer to the data to be sent
 * @param[in] len Length of the data in bytes
 * @return Number of bytes sent, or negative value on error
 */
int sl_udp_packet_send_v6(struct uip_udp_conn *c, const uint8_t *data, uint16_t len);

/**
 * @brief Convert 32-bit IP address to byte array
 *
 * Converts a 32-bit IPv4 address to a 4-byte array in network byte order.
 *
 * @param[in] ip 32-bit IPv4 address
 * @param[out] bytes Output buffer for the 4-byte array (must be at least 4 bytes)
 */
void uint32_to_ip(uint32_t ip, char *bytes);

/**
 * @brief Process an IPv6 ZIP packet
 *
 * Creates a TCP/IP buffer from an IPv6 ZIP packet for processing by the Z-Wave stack.
 *
 * @param[in] in Pointer to the IPv6 address structure
 * @param[in] in_port Source port number
 * @param[in] lip Local IP address string
 * @param[in] buf Pointer to the packet data
 * @param[in] len Length of the packet data
 * @return Pointer to allocated TCP/IP buffer or NULL on failure
 */
sl_tcpip_buf_t* sl_zip_packet_v6(struct in6_addr *in,
                                 uint16_t in_port,
                                 char *lip,
                                 uint8_t *buf,
                                 uint16_t len);

/**
 * @brief Process an IPv4 ZIP packet
 *
 * Creates a TCP/IP buffer from an IPv4 ZIP packet for processing by the Z-Wave stack.
 * In this version, the destination node ID is included in the UDP message.
 *
 * @param[in] in Pointer to the IPv4 socket address structure
 * @param[in] buf Pointer to the packet data
 * @param[in] len Length of the packet data
 * @return Pointer to allocated TCP/IP buffer or NULL on failure
 */
sl_tcpip_buf_t* sl_zip_packet_v4(struct sockaddr_in *in, uint8_t *buf, uint16_t len);

#endif /* APPS_NET_SL_UDP_PORT_H_ */
