/***************************************************************************/ /**
 * @file sl_ts_common.c
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
#include <stdint.h>
#include "sl_common_log.h"
#include "sl_common_config.h"
#include "sl_rd_types.h"
#include "sl_gw_info.h"

#include "Z-Wave/include/ZW_classcmd.h"
#include "utls/sl_zw_validator.h"
#include "utls/sl_node_sec_flags.h"
#include "utls/zgw_crc.h"

#include "sl_ts_param.h"

#include "sl_ts_common.h"

void ts_set_std(ts_param_t *p, nodeid_t dnode)
{
  p->dendpoint = 0;
  p->sendpoint = 0;
  p->snode     = MyNodeID;
  p->dnode     = dnode;
  nodemask_clear(p->node_list);
  p->rx_flags = 0;
  p->tx_flags = TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_AUTO_ROUTE
                | TRANSMIT_OPTION_EXPLORE;
  p->scheme                = AUTO_SCHEME;
  p->discard_timeout       = 0;
  p->force_verify_delivery = FALSE;
  p->is_mcast_with_folloup = FALSE;
  p->is_multicommand       = FALSE;
}

/**
 * Returns true if source and destinations are identical
 */
uint8_t ts_param_cmp(ts_param_t *a1, const ts_param_t *a2)
{
  return (a1->snode == a2->snode && a1->dnode == a2->dnode
          && a1->sendpoint == a2->sendpoint && a1->dendpoint == a2->dendpoint);
}

security_scheme_t highest_scheme(uint8_t scheme_mask)
{
  if (scheme_mask & NODE_FLAG_SECURITY2_ACCESS) {
    return SECURITY_SCHEME_2_ACCESS;
  } else if (scheme_mask & NODE_FLAG_SECURITY2_AUTHENTICATED) {
    return SECURITY_SCHEME_2_AUTHENTICATED;
  } else if (scheme_mask & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
    return SECURITY_SCHEME_2_UNAUTHENTICATED;
  } else if (scheme_mask & NODE_FLAG_SECURITY0) {
    return SECURITY_SCHEME_0;
  } else {
    return NO_SCHEME;
  }
}

security_scheme_t
zw_scheme_select(const ts_param_t *param, const uint8_t *data, uint8_t len)
{
  (void)data; // Mark unused parameter
  uint8_t dst_scheme_mask = 0; //Mask of schemes supported by destination
  uint8_t src_scheme_mask = 0;
  security_scheme_t dst_highest_scheme = highest_scheme(dst_scheme_mask);
  /* Check that this node node has at least one security scheme */

  if ((len < 2) || (dst_scheme_mask & NODE_FLAG_KNOWN_BAD)
      || (src_scheme_mask & NODE_FLAG_KNOWN_BAD)) {
    if ((param->scheme == USE_CRC16)
        && sl_cmdclass_supported(param->dnode, COMMAND_CLASS_CRC_16_ENCAP)
        && (dst_highest_scheme == NO_SCHEME)) {
      DBG_PRINTF("Node has NODE_FLAG_KNOWN_BAD set in either dst_scheme_mask or"
                 " src_scheme_mask, but supports CRC16");
      return USE_CRC16;
    } else {
      DBG_PRINTF("Node has NODE_FLAG_KNOWN_BAD set in either dst_scheme_mask or"
                 " src_scheme_mask or frame length is less than 2\n");

      return NO_SCHEME;
    }
  }
  dst_scheme_mask &= src_scheme_mask;

  switch (param->scheme) {
    case AUTO_SCHEME:
      LOG_PRINTF("DS default SECURITY_SCHEME_0\n");
      return SECURITY_SCHEME_0;
    case NO_SCHEME:
      return NO_SCHEME;
    case USE_CRC16:
      return USE_CRC16;
    case SECURITY_SCHEME_2_ACCESS:
      break;
    case SECURITY_SCHEME_2_AUTHENTICATED:
      break;
    case SECURITY_SCHEME_2_UNAUTHENTICATED:
      break;
    case SECURITY_SCHEME_0:
      if (dst_scheme_mask & NODE_FLAG_SECURITY0) {
        return param->scheme;
      }
      break;
    default:
      break;
  }
  WRN_PRINTF("Scheme %x NOT supported by destination %i \n",
             param->scheme,
             param->dnode);
  return param->scheme;
}

void ts_param_make_reply(ts_param_t *dst, const ts_param_t *src)
{
  dst->snode           = src->dnode;
  dst->dnode           = src->snode;
  dst->sendpoint       = src->dendpoint;
  dst->dendpoint       = src->sendpoint;
  dst->scheme          = zw_scheme_select(src, 0, 2);
  dst->discard_timeout = 0;

  dst->tx_flags =
    ((src->rx_flags & RECEIVE_STATUS_LOW_POWER) ? TRANSMIT_OPTION_LOW_POWER
     : 0)
    | TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_EXPLORE
    | TRANSMIT_OPTION_AUTO_ROUTE;
}

#define STR_CASE(x) \
  case x:           \
    return #x;

const char* network_scheme_name(uint8_t scheme)
{
  static char message[25];
  switch (scheme) {
    STR_CASE(NO_SCHEME)
    STR_CASE(SECURITY_SCHEME_0)
    STR_CASE(SECURITY_SCHEME_2_UNAUTHENTICATED)
    STR_CASE(SECURITY_SCHEME_2_AUTHENTICATED)
    STR_CASE(SECURITY_SCHEME_2_ACCESS)
    STR_CASE(SECURITY_SCHEME_UDP)
    STR_CASE(USE_CRC16)

    default:
      snprintf(message, sizeof(message), "%d", scheme);
      return message;
  }
}
