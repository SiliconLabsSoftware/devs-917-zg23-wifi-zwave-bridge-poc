/*******************************************************************************
 * @file  ZW_udp_server.h
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
#ifndef ZW_UDP_SERVER_H_
#define ZW_UDP_SERVER_H_

#include <stdint.h>
#include <Common/sl_common_type.h>
#include <Common/sl_uip_def.h>
#include <transport/sl_ts_param.h>
/** \ingroup processes
 * \defgroup ZIP_Udp Z/IP UDP process
 * Handles the all Z/IP inbound and outbout UDP communication
 * Manages the Z/IP sessions for the Zipgateway
 * @{
 */
/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/
#define ZWAVE_PORT 4123
#define DTLS_PORT 41230

typedef struct {
  union {
    /**
     * Convenience attribute.
     */
    struct uip_udp_conn conn;
    struct {
      /**
       * Local ip address
       */
      uip_ip6addr_t lipaddr;
      /**
       * Remote IP address
       */
      uip_ip6addr_t ripaddr;
      /**
       *  The local port number in network byte order.
       */
      uint16_t lport;
      /**
       * The remote port number in network byte order.
       */
      uint16_t rport;
    };
  };
  /**
   * remote endpoint
   */
  uint8_t rendpoint;

  /**
   * local endpoint
   *
   */
  uint8_t lendpoint;
  /**
   * Sequence number when sending UDP frames
   */
  uint8_t seq;
  /**
   * Security scheme when sending Z-Wave frames
   */
  security_scheme_t scheme;
  /**
   * rx flags when receive as a Z-Wave frame
   */
  uint8_t rx_flags;
  /**
   * tx flags when sending as a Z-Wave frame
   */
  uint8_t tx_flags;
} zwave_udp_session_t;

typedef struct {
  uint8_t type;
  uint8_t length;
  uint8_t value[1];
} tlv_t;

/**
 * Structure describing a Z-Wave or Z/IP connection, with source and destination
 * ips/nodids/endpoint. Together with specific parameter such as receiver flags.
 */
typedef zwave_udp_session_t zwave_connection_t;

/**
 * Returns true if the two connections are identical, i has the same source and destination addresses.
 */
int zwave_connection_compare(zwave_connection_t* a, zwave_connection_t *b);

typedef enum {
  RES_ACK, RES_NAK, RES_WAITNG, RES_OPT_ERR,
} zwave_udp_response_t;

/**
 * Send a udp ACK NAK or waiting
 */
void send_udp_ack(zwave_udp_session_t* s, zwave_udp_response_t res);

/**
 * Wrapper function to Send package at an ordinary Z-wave package or as a
 * Z/IP UDP package depending on destination address.
 *
 * If the first 15 bytes of the destination address is all zeroes then
 * the package is sent as a ordinary Z-wave package. In this case
 * \see sl_zw_send_data_udp
 * \see ZW_SendData
 *
 * \param c connection describing the source and destination for this package
 * \param dataptr pointer to the data being sent
 * \param datalen length of the data being sent
 * \param cbFunc Callback to be called when transmission is complete
 */
extern void
sl_zw_send_zip_data(zwave_connection_t *c, const  void *dataptr, uint16_t datalen,
                    ZW_SendDataAppl_Callback_t cbFunc);

/**
 * Send a Z-Wave udp frame, but with the ACK flag set. No retransmission will be attempted
 * See \ref sl_zw_send_zip_data
 */
extern void
sl_zw_send_data_zip_ack(zwave_connection_t *c, const void *dataptr, uint8_t datalen, void
                        (*cbFunc)(uint8_t, void*, TX_STATUS_TYPE *));

extern void (*uip_completedFunc)(uint8_t, void*);

/**
 * \return TRUE if the specified address is a Z-Wave address ie a 0\::node address
 */
#if IPV6
#define ZW_IsZWAddr(a)      \
  (                         \
    (((a)->u8[1]) == 0)     \
    && (((a)->u16[1]) == 0) \
    && (((a)->u16[2]) == 0) \
    && (((a)->u16[3]) == 0) \
    && (((a)->u16[4]) == 0) \
    && (((a)->u16[5]) == 0) \
    && (((a)->u16[6]) == 0) \
    && (((a)->u8[14]) == 0) \
    && (((a)->u8[15]) == 0))
#else
#define ZW_IsZWAddr(a) 0
#endif

/**
 * Input function for RAW Z-Wave udp frames. This function parses the COMMAND_CLASS_ZIP header
 * and manages UDP sessions.
 * \param c UDP connection the frame ware received on.
 * \param data pointer to the udp data
 * \param len length of the udp data
 * \param received_secure TRUE if this was a UDP frame which has been decrypted with DTLS.
 */
void sl_udp_command_handler(struct uip_udp_conn* c, const uint8_t* data, uint16_t len, uint8_t received_secure);

/**
 * Send package using the given UDP connection. The package may be DTLS encrypted.
 *
 * \param c UDP connection the frame ware received on.
 * \param data pointer to the udp data
 * \param len length of the udp data
 * @param cbFunc to callback function
 * @param user: user pointer
 */

void udp_send_wrap(struct uip_udp_conn* c, const void* data, uint16_t len, void (*cbFunc)(uint8_t, void*), void *user);

/**
 * Convert zwave udp packet EFI extensions to a security scheme
 * \param ext1 fist ext byte
 * \param ext2 second ext byte
 */
security_scheme_t efi_to_shceme(uint8_t ext1, uint8_t ext2);

void sl_zw_udp_init(void);

void sl_zw_udp_handler(struct uip_udp_conn *c, uint8_t *uip_appdata, uint16_t uip_datalen);

int sl_udp_packet_send_v6(struct uip_udp_conn *c, const uint8_t *data, uint16_t len);

/** @} */
#endif /* ZW_UDP_SERVER_H_ */
