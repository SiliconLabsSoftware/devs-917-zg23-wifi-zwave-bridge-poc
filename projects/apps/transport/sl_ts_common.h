/***************************************************************************/ /**
 * @file sl_ts_common.h
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

#ifndef SL_TS_COMMON_H
#define SL_TS_COMMON_H

#include "sl_ts_param.h"
#include "apps/Z-Wave/include/ZW_typedefs.h"

security_scheme_t
zw_scheme_select(const ts_param_t *param, const uint8_t *data, uint8_t len);

void ts_param_make_reply(ts_param_t *dst, const ts_param_t *src);

int sl_cmdclass_supported(nodeid_t nodeid, WORD cls);

int sl_cmdclass_secure_supported(nodeid_t nodeid, WORD class);

BYTE sl_get_cache_entry_flag(nodeid_t nodeid);

uint8_t ts_param_cmp(ts_param_t *a1, const ts_param_t *a2);

void ts_set_std(ts_param_t *p, nodeid_t dnode);

security_scheme_t highest_scheme(uint8_t scheme_mask);

const char* network_scheme_name(uint8_t scheme);

#endif // SL_TS_COMMON_H
