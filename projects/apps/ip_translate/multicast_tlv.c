/*******************************************************************************
 * @file  multicast_tlv.c
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
#include <stdlib.h>

#include "utls/ipv6_utils.h" /* sl_node_of_ip */
#include "sl_common_log.h"
#include "Net/ZW_udp_server.h"
#include "sl_common_config.h"
#include "multicast_tlv.h"

BYTE cur_send_ima;

int parse_CC_ZIP_EXT_HDR(BYTE* payload, uint16_t length, zwave_connection_t *zw_conn, uint16_t *ext_hdr_size)
{
  tlv_t* tlv = (tlv_t*) payload;

  BYTE* tail_payload = payload + length;
  *ext_hdr_size = length;

  cur_send_ima = 0;
  while ( (BYTE*)tlv < tail_payload) {
    switch (tlv->type & ZIP_EXT_HDR_OPTION_TYPE_MASK) {
      case INSTALLATION_MAINTENANCE_GET:
        cur_send_ima = TRUE;
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;
      case ENCAPSULATION_FORMAT_INFO:
        if (tlv->length < 2) {
          return OPT_ERROR;
        }
        zw_conn->scheme = efi_to_shceme(tlv->value[0], tlv->value[1]);
        DBG_PRINTF("zw.scheme: %d\n", zw_conn->scheme);
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;

      default:
        if (tlv->type & ZIP_EXT_HDR_OPTION_CRITICAL_FLAG) {
          ERR_PRINTF("Unsupported critical option %i\n", (tlv->type & ZIP_EXT_HDR_OPTION_TYPE_MASK));
          /* Send option error */
          return OPT_ERROR;
        }
        tlv = (tlv_t*)((BYTE*) tlv + (tlv->length + 2));
        break;
    }
  }

  if ((BYTE*)tlv != tail_payload) {
    ERR_PRINTF("Invalid extended header\n");
    return DROP_FRAME;
  }
  return ZIP_EXT_HDR_SUCCESS;
}

uint16_t add_ext_header(ZW_COMMAND_ZIP_PACKET* pkt, int bufsize, uint8_t opt_type, uint8_t* opt_val, uint16_t opt_len, uint8_t opt_len_size)
{
  uint16_t payload_size_before  = 0;
  uint16_t payload_size_after   = 0;
  uint16_t required_buffer_size = 0;
  uint16_t new_option_offset    = 0;
  uint8_t *p_length_option      = NULL;

  ASSERT((uint32_t)bufsize >= sizeof(ZW_COMMAND_ZIP_PACKET));
  ASSERT(opt_len_size == 1 || opt_len_size == 2);

  if (opt_len_size == 1 && opt_len > 255) {
    ERR_PRINTF("Can't write an extended header option length larger than 255 to a single byte!\n");
    return 0;
  }

  /* Do we already have one or more extended header options? */
  if (pkt->flags1 & ZIP_PACKET_FLAGS1_HDR_EXT_INCL) {
    /* Check for presence of an extended header length option */
    if (pkt->payload[0] == 0xff
        && (pkt->payload[1] & ZIP_EXT_HDR_OPTION_TYPE_MASK) == EXT_ZIP_PACKET_HEADER_LENGTH) {
      /* Get payload size from two-byte length field in extended
       * header length option
       */
      p_length_option = &pkt->payload[1];

      /* 2'nd byte of the length option is the length of the option
       * itself (must always be 2)
       */
      ASSERT(p_length_option[1] == 2);

      /* 3'rd byte = length MSB, 4'th byte = length LSB */
      payload_size_before = (p_length_option[2] << 8) | p_length_option[3];
    } else {
      /* Get payload size from "standard" single byte
       * header extension length field
       */
      payload_size_before = pkt->payload[0];
    }

    new_option_offset = payload_size_before;
  } else {
    /* We are adding the very first extended header to the packet
     *
     * First byte of the payload is the 1-byte length. We'll update
     * it later, but for now we need to ensure the new option is
     * added after.
     */
    new_option_offset = 1;
  }

  /* Payload size after adding new option */
  payload_size_after = new_option_offset
                       + 1              // One byte for option type
                       + opt_len_size   // One or two bytes for length
                       + opt_len;       // The header option data

  required_buffer_size = ZIP_HEADER_SIZE + payload_size_after;

  /* Do we need to add the four byte length option? */
  if (payload_size_after > 255 && !p_length_option) {
    required_buffer_size += 4;
  }

  if (required_buffer_size > bufsize) {
    ERR_PRINTF("Can't add extended header. Packet buffer too small!\n");
    return 0;
  }

  /* Buffer size OK. Now start appending the new option to buf */

  uint8_t *p = pkt->payload + new_option_offset;

  *p++ = opt_type;

  /* Should the length be written to one or two bytes? */
  if (opt_len_size == 2) {
    *p++ = opt_len >> 8;    // Length 1 (MSB)
  }
  *p++ = opt_len & 0x00ff;  // Length (single byte) or Length 2 (LSB)

  memcpy(p, opt_val, opt_len);

  /* Do we need two bytes for the total payload size? */
  if (payload_size_after > 255) {
    if (!p_length_option) {
      /* The four byte length option is not already there. Make space for it
       * by moving the memory following the "legacy" length field payload[0]
       * down by four bytes (payload[0] is part of the payload counted by
       * "payload_size_after" but it's not part of the memory to move so we
       * subtract one when calling memmove)
       */

      memmove(&pkt->payload[5], &pkt->payload[1], payload_size_after - 1);
      p_length_option = &pkt->payload[1];
      payload_size_after += 4;
    }

    pkt->payload[0]    = 255;
    p_length_option[0] = ZIP_EXT_HDR_OPTION_CRITICAL_FLAG | EXT_ZIP_PACKET_HEADER_LENGTH;
    p_length_option[1] = 2;
    p_length_option[2] = payload_size_after >> 8;      // MSB
    p_length_option[3] = payload_size_after & 0x00ff;  // LSB
  } else {
    pkt->payload[0] = payload_size_after;
  }

  pkt->flags1 |= ZIP_PACKET_FLAGS1_HDR_EXT_INCL;

  /* Return number of bytes added to zip packet */
  return payload_size_after - payload_size_before;
}
