/*******************************************************************************
 * @file  sl_ipnode_util.c
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
#include "ipv6_utils.h"
#include "sl_tcpip_def.h"
#include "sl_ipnode_utils.h"

sl_tcpip_buf_t bkp_con;
char bkp_zip[BKP_ZIP_SIZE];
uint32_t bkp_zip_len = 0;

uint8_t
is_local_address(uip_ipaddr_t *ip)
{
  return sl_node_of_ip(ip) != 0;
}
