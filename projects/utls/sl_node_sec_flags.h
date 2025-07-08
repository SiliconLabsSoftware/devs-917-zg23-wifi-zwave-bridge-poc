/*******************************************************************************
 * @file  sl_node_sec_flags.h
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
#ifndef SL_NODE_SEC_FLAGS_H
#define SL_NODE_SEC_FLAGS_H

#define isNodeBad(n) ((sl_get_cache_entry_flag(n) & NODE_FLAG_KNOWN_BAD) != 0)
#define  isNodeSecure(n) ( (sl_get_cache_entry_flag(n) & (NODE_FLAG_SECURITY0 | NODE_FLAG_KNOWN_BAD)) == NODE_FLAG_SECURITY0)

#define NODE_FLAG_SECURITY0 0x01
#define NODE_FLAG_KNOWN_BAD 0x02

#define NODE_FLAG_INFO_ONLY                 0x08 /* Only probe the node info */
#define NODE_FLAG_SECURITY2_UNAUTHENTICATED 0x10
#define NODE_FLAG_SECURITY2_AUTHENTICATED   0x20
#define NODE_FLAG_SECURITY2_ACCESS          0x40

#define NODE_FLAGS_SECURITY2                                               \
  (NODE_FLAG_SECURITY2_UNAUTHENTICATED | NODE_FLAG_SECURITY2_AUTHENTICATED \
   | NODE_FLAG_SECURITY2_ACCESS)

#define NODE_FLAGS_SECURITY \
  (NODE_FLAG_SECURITY0 | NODE_FLAG_KNOWN_BAD | NODE_FLAGS_SECURITY2)

#endif // SL_NODE_SEC_FLAGS_H
