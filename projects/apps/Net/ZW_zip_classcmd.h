/*******************************************************************************
 * @file  ZW_zip_classcmd.h
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
#ifndef ZW_ZIP_CLASSCMD_H_
#define ZW_ZIP_CLASSCMD_H_

#include "ZW_typedefs.h"
#include "sl_uip_def.h"

/** /defgroup ZW_CMD_Handler Z/IP Command handler
 */

/****************************************************************************/
/*                              INCLUDE FILES                               */
/****************************************************************************/

/****************************************************************************/
/*                     EXPORTED TYPES and DEFINITIONS                       */
/****************************************************************************/
#define COMMAND_CLASS_ZIP_DEMO 0xAE

/* Command codes for COMMAND_CLASS_ZIP_DEMO */
#define ZIP_CMD_PREFIX_SET 0x0

#define ANYSIZE_ARRAY 1 /* see http://blogs.msdn.com/b/oldnewthing/archive/2004/08/26/220873.aspx */

typedef struct _zip_prefix_t_ {
  BYTE      length;                       /* Number of bytes in prefix, the (16-length)
                                           *   remaining  bytes are assumed to be 0 */
  BYTE      prefix[ANYSIZE_ARRAY];        /* Prefix in network byte order */
} zip_prefix_t;

typedef struct _ZW_COMMAND_ZIP_PREFIX_SET_ {
  BYTE          cmdClass;                       /* The command class */
  BYTE          cmd;                            /* The command */
  zip_prefix_t  zip_prefix;
} ZW_COMMAND_ZIP_PREFIX_SET;

#define ZIP_PACKET_FLAGS0_ACK_REQ  0x80
#define ZIP_PACKET_FLAGS0_ACK_RES  0x40
#define ZIP_PACKET_FLAGS0_NACK_RES 0x20

#define ZIP_PACKET_FLAGS0_NACK_WAIT (1 << 4) //Waiting
#define ZIP_PACKET_FLAGS0_NACK_QF   (1 << 3) //Queue full
#define ZIP_PACKET_FLAGS0_NACK_OERR (1 << 2) //Option error

#define ZIP_PACKET_FLAGS1_HDR_EXT_INCL     0x80
#define ZIP_PACKET_FLAGS1_ZW_CMD_INCL      0x40
#define ZIP_PACKET_FLAGS1_MORE_INFORMATION 0x20
#define ZIP_PACKET_FLAGS1_SECURE_ORIGIN    0x10

typedef struct _ZW_COMMAND_ZIP_PACKET_ {
  BYTE      cmdClass;            /* The command class  = COMMAND_CLASS_ZIP */
  BYTE      cmd;                 /* The command = COMMAND_ZIP_PACKET */
  BYTE      flags0;
  BYTE      flags1;
  BYTE      seqNo;
  BYTE      sEndpoint;          /* Source endpoint */
  BYTE      dEndpoint;          /* Destination endpoint */
  BYTE      payload[ANYSIZE_ARRAY];    /* Pointer to the payload of the message */
} ZW_COMMAND_ZIP_PACKET;

#define ZIP_HEADER_SIZE (sizeof(ZW_COMMAND_ZIP_PACKET) - 1) /* subtract variable length array at end */

typedef struct _s_ {
  BYTE          cmdClass;
  BYTE          cmd;
  BYTE          groupingIdentifier;
  uip_ip6addr_t resourceIP;
  BYTE          endpoint;
} __attribute__((packed)) ZW_COMMAND_IP_ASSOCIATION_SET;

 #define IP_ASSOC_SET_FIXED_SIZE sizeof(ZW_COMMAND_IP_ASSOCIATION_SET)

typedef struct _ZW_COMMAND_IP_ASSOCIATION_REMOVE_{
  uint8_t cmd_class;
  uint8_t cmd;
  uint8_t grouping;
  uip_ip6addr_t ip_addr;
  uint8_t endpoint;
} __attribute__((packed)) ZW_COMMAND_IP_ASSOCIATION_REMOVE;

/****************************************************************************/
/*                              EXPORTED DATA                               */
/****************************************************************************/

/****************************************************************************/
/*                           EXPORTED FUNCTIONS                             */
/****************************************************************************/

#endif /* ZW_ZIP_CLASSCMD_H_ */
