/*******************************************************************************
 * @file  sl_tcpip_def.h
 * @brief Define common data structure for TCPIP
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

#ifndef APPS_COMMON_SL_TCPIP_DEF_H_
#define APPS_COMMON_SL_TCPIP_DEF_H_

#include "sl_uip_def.h"
#include "Net/ZW_zip_classcmd.h"
#include "Net/ZW_udp_server.h"

typedef struct {
  zwave_connection_t zw_con;
  uint32_t tcpip_proto;
  uint32_t icmp_type;
  ZW_COMMAND_ZIP_PACKET *zip_packet;
  char *zip_data;
  uint32_t zip_data_len;
} sl_tcpip_buf_t;

#endif /* APPS_COMMON_SL_TCPIP_DEF_H_ */
