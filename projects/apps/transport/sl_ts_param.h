/***************************************************************************/ /**
 * @file sl_ts_param.h
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

#ifndef SL_TS_PARAM_H
#define SL_TS_PARAM_H

#include <stdbool.h>
#include "sl_common_type.h"
#include "sl_rd_types.h"
#include "ZW_typedefs.h"
#include "utls/zgw_nodemask.h"
#include "sl_sleeptimer.h"

#define MAX_ENCRYPTED_MSG_SIZE 128

typedef void( *ZW_SendDataAppl_Callback_t)(BYTE txStatus, void* user, TX_STATUS_TYPE *txStatEx);

/**
 * Enumeration of security schemes, this must map to the bit numbers in NODE_FLAGS
 */
typedef enum SECURITY_SCHEME {
  /** Do not encrypt message */
  NO_SCHEME = 0xFF,

  /** Let the SendDataAppl layer decide how to send the message */
  AUTO_SCHEME = 0xFE,

  NET_SCHEME = 0xFC, //Highest network scheme
  USE_CRC16 = 0xFD, //Force use of CRC16, not really a security scheme..

  /** Send the message with security Scheme 0 */
  SECURITY_SCHEME_0 = 7,

  /** Send the message with security Scheme 2 */
  SECURITY_SCHEME_2_UNAUTHENTICATED = 1,
  SECURITY_SCHEME_2_AUTHENTICATED = 2,
  SECURITY_SCHEME_2_ACCESS = 3,
  SECURITY_SCHEME_UDP = 4
} security_scheme_t;

//typedef uint8_t node_list_t[ZW_MAX_NODES/8];

/**
 * Structure describing how a package was received / should be transmitted
 */
typedef struct tx_param {
  /**
   * Source node
   */
  nodeid_t snode;
  /**
   * Destination node
   */
  nodeid_t dnode;

  /**
   * Source endpoint
   */
  uint8_t sendpoint;

  /**
   * Destination endpoint
   */
  uint8_t dendpoint;

  /**
   * Transmit flags
   * see txOptions in \ref ZW_SendData_Bridge
   */
  uint8_t tx_flags;

  /**
   * Receive flags
   * see rxStatus in \ref SerialAPI_Callbacks::ApplicationCommandHandler
   */
  uint8_t rx_flags;

  /**
   * Security scheme used for this package
   */
  security_scheme_t scheme; // Security scheme

  /**
   * Nodemask used when sending multicast frames.
   */
  nodemask_t node_list;
  /**
   * True if this is a multicast with followup enabled.
   */
  BOOL is_mcast_with_folloup;

  /**
   * Force verify delivery
   */
  BOOL force_verify_delivery;

  /**
   */
  BOOL is_multicommand;

  /**
   *   Maximum time ticks this transmission is allowed to spend in queue waiting to be processed by SerialAPI before it is dropped.
   *   There are CLOCK_SECOND ticks in a second. 0 means never drop.
   */
  uint16_t discard_timeout;    /* not using clock_time_t to avoid dependency on contiki-conf.h */
} ts_param_t;

typedef enum {
  NONCE_GET,
  NONCE_GET_SENT,
  ENC_MSG,
  ENC_MSG_SENT,
  ENC_MSG2,
  ENC_MSG2_SENT,
  TX_DONE,
  TX_FAIL
} tx_state_t;

typedef struct sec_tx_session {
  ts_param_t param;
  const uint8_t *data; //Pointer the data to be encrypted
  uint8_t data_len;    //Length of the data to be encrypted
  tx_state_t state;
  uint8_t tx_code;
  uint32_t transition_time;
  sl_sleeptimer_timer_handle_t timer; //Session timer
  uint8_t crypted_msg[46];
  uint8_t seq; //Sequence number used in multisegment messages
  void *user;  //User supplied pointer which is returned in the callback
  ZW_SendDataAppl_Callback_t
    callback;   //Callback function, called when the session has been terminated
  TX_STATUS_TYPE
    tx_ext_status; // Extended TX status, most notably IMA information
} sec_tx_session_t;

/*
 *
 * Jeg kunne lave en ordenlig rx session hvis man m���tte rapotere samme nonce flere gange
 * hvis den ikke var brugt.
 *
 * Jeg sender NONCE_GET, men jeg misser en ack s��� den bliver sendt flere gange
 * Jeg modtager derfor flere reports. Kunne disse reports ikke indholde samme nonce?
 * Jeg kan ikke se problemet, jeg koder
 *
 */
typedef enum {
  RX_INIT,
  RX_ENC1,
  RX_ENC2,
  RX_SESSION_DONE
} rx_session_state_t;

typedef struct {
  uint8_t snode;
  uint8_t dnode;
  rx_session_state_t state;
  uint8_t seq_nr;
  uint8_t msg[MAX_ENCRYPTED_MSG_SIZE];
  uint8_t msg_len;
  clock_time_t timeout;
} rx_session_t;

typedef struct _AUTHDATA_ {
  uint8_t sh;             /* Security Header for authentication */
  uint8_t senderNodeID;   /* Sender ID for authentication */
  uint8_t receiverNodeID; /* Receiver ID for authentication */
  uint8_t payloadLength;  /* Length of authenticated payload */
} auth_data_t;

#endif // SL_TS_PARAM_H
