/*******************************************************************************
 * @file  sl_gw_info.c
 * @brief Store global information
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
#include "sl_common_config.h"
#include "sl_common_log.h"
#include "sl_rd_types.h"
#include "modules/sl_si917_net.h"
#include "sl_gw_info.h"

#define SL_USOL_IP_DEFAULT      "::ffff:0a00:020f"
#define SL_PAN_PREFIX_DEFAULT   "fd00:bbbb:1::01"
#define SL_LAN_PREFIX_DEFAULT   "fd00:aaaa:1::03"
#define SL_GW_PREFIX_DEFAULT    "fd00:aaaa:1::1234"
#define SL_TUN_PREFIX_DEFAULT   "2000::"

nodeid_t MyNodeID = 1;
uint32_t homeID;

sl_router_config_t router_cfg;
sl_si917_gw_t sl_gw_info;

/**
 * @brief Convert a hexadecimal character to its integer value
 *
 * This function converts a single hexadecimal character to its integer equivalent.
 *
 * @param[in] c The hexadecimal character to convert
 * @return The integer value of the character, or -1 if invalid
 */
static int sl_hex2int(char c);

/**
 * @brief Convert an IPv6 address string to binary representation
 *
 * This function parses an IPv6 address string and converts it to its binary
 * representation for use with the networking stack.
 *
 * @param[in] addrstr The IPv6 address string to convert
 * @param[out] ipaddr Pointer to the output binary IPv6 address structure
 * @return 0 on success, non-zero on failure
 */
int sl_uiplib_ipaddrconv(const char *addrstr, uip_ipaddr_t *ipaddr)
{
  uint16_t value;
  int tmp, zero;
  size_t len;
  char c = 0;

  value = 0;
  zero  = -1;
  for (len = 0; len < sizeof(uip_ipaddr_t) - 1; addrstr++) {
    c = *addrstr;
    if (c == ':' || c == '\0') {
      ipaddr->u8[len]     = (value >> 8) & 0xff;
      ipaddr->u8[len + 1] = value & 0xff;
      len += 2;
      value = 0;

      if (c == '\0') {
        break;
      }
      /* Zero compression */
      if (zero < 0 && *(addrstr + 1) == ':') {
        zero = len;
      }

      if (*(addrstr + 1) == ':') {
        addrstr++;
      }
    } else {
      tmp = sl_hex2int(c);
      if (tmp < 0) {
        LOG_PRINTF("uiplib: illegal char: '%c'\n", c);
        return 0;
      }
      value = (value << 4) + (tmp & 0xf);
    }
  }
  if (c != '\0') {
    LOG_PRINTF("uiplib: too large address\n");
    return 0;
  }
  if (len < sizeof(uip_ipaddr_t)) {
    if (zero < 0) {
      LOG_PRINTF("uiplib: too short address\n");
      return 0;
    }
    memmove(&ipaddr->u8[zero + sizeof(uip_ipaddr_t) - len],
            &ipaddr->u8[zero],
            len - zero);
    memset(&ipaddr->u8[zero], 0, sizeof(uip_ipaddr_t) - len);
  }
  return 0;
}

/**
 * @brief Convert a hexadecimal character to its integer value
 *
 * This function converts a single hexadecimal character to its integer equivalent.
 * It handles both uppercase and lowercase hex digits ('0'-'9', 'A'-'F', 'a'-'f').
 *
 * @param[in] c The hexadecimal character to convert
 * @return The integer value of the character, or -1 if invalid
 */
static int sl_hex2int(char c)
{
  if (c >= '0' && c <= '9') {
    return c - '0';
  } else if (c >= 'a' && c <= 'f') {
    return c - 'a' + 0xa;
  } else if (c >= 'A' && c <= 'F') {
    return c - 'A' + 0xa;
  } else {
    return -1;
  }
}

/**
 * @brief Initialize the Z-Wave gateway configuration with default values
 *
 * This function initializes the router_cfg structure with default values
 * including network addresses, security settings, and device parameters.
 * It sets up IPv6 addresses for the gateway, LAN, PAN, and tunneling interfaces.
 */
void sl_config_init(void)
{
  int val;
  const char *s;

  router_cfg.version[0] = FW_VERSION_MAJOR;
  router_cfg.version[1] = FW_VERSION_MINOR;
  router_cfg.version[2] = FW_VERSION_PATCH;

  memset(&router_cfg, 0, sizeof(router_cfg));

  /* Use a defined IP address that has been reviewed for security */
  sl_uiplib_ipaddrconv(SL_USOL_IP_DEFAULT, &(router_cfg.unsolicited_dest));
  router_cfg.unsolicited_port = 37263;

  router_cfg.portal_url[0] = 0;
  router_cfg.portal_portno = 44123;

  router_cfg.log_level = 1;

  /* ULA addresses for local network communications (security reviewed) */
  sl_uiplib_ipaddrconv(SL_PAN_PREFIX_DEFAULT, &(router_cfg.cfg_pan_prefix)); /* Private IPv6 ULA for PAN */
  sl_uiplib_ipaddrconv(SL_LAN_PREFIX_DEFAULT, &(router_cfg.cfg_lan_addr)); /* Private IPv6 ULA for LAN */
  router_cfg.cfg_lan_prefix_length = 64;

  sl_uiplib_ipaddrconv(SL_GW_PREFIX_DEFAULT, &(router_cfg.gw_addr)); /* Private IPv6 ULA for gateway */
  router_cfg.tun_prefix_length = 128;

  /* IPv6 2000:: prefix for tunneling (security reviewed) */
  sl_uiplib_ipaddrconv(SL_TUN_PREFIX_DEFAULT, &(router_cfg.tun_prefix));

  router_cfg.ipv4disable     = 0;
  router_cfg.client_key_size = 1024;

  router_cfg.manufacturer_id  = 0;
  router_cfg.product_id       = 1;
  router_cfg.product_type     = 1;
  router_cfg.hardware_version = 1;
  router_cfg.device_id_len    = 0;

  s                  = "123456789012345678901234567890AA";
  router_cfg.psk_len = 0;
  while (*s && router_cfg.psk_len < sizeof(router_cfg.psk)) {
    val = sl_hex2int(*s++);
    if (val < 0) {
      break;
    }
    router_cfg.psk[router_cfg.psk_len] = ((val) & 0xf) << 4;
    val                                = sl_hex2int(*s++);
    if (val < 0) {
      break;
    }
    router_cfg.psk[router_cfg.psk_len] |= (val & 0xf);

    router_cfg.psk_len++;
  }

  /*Parse extra classes*/
  uint8_t ex[] = { 0x85, 0x8E, 0x59, 0x5A };
  memcpy(router_cfg.extra_classes, ex, 4);

  /* Assume powerlevel setting exists */
  router_cfg.is_powerlevel_set = 1;
  router_cfg.tx_powerlevel     = 0;
  /* Parse normal TX power into string */
  router_cfg.rfregion                 = 0;
  router_cfg.zw_lbt                   = 64;
  router_cfg.is_max_lr_powerlevel_set = 0;

  router_cfg.is_max_lr_powerlevel_set = 1;

  // DS we fix lan & pan address here.
  router_cfg.lan_prefix_length = router_cfg.cfg_lan_prefix_length;
  uip_ipaddr_copy(&router_cfg.lan_addr, &router_cfg.cfg_lan_addr);

  uip_ipaddr_copy(&router_cfg.pan_prefix, &router_cfg.cfg_pan_prefix);
}
