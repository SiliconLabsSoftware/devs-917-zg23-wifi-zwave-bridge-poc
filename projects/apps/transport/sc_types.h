/***************************************************************************/ /**
 * @file sl_ts_types.h
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

#ifndef SC_TYPES_H_
#define SC_TYPES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>

typedef int_fast16_t  sc_short;
typedef uint_fast16_t sc_ushort;
typedef int32_t       sc_integer;
typedef uint32_t      sc_uinteger;
typedef double        sc_real;
typedef char*         sc_string;

typedef void*         sc_eventid;

#ifdef __cplusplus
}
#endif
#define sc_boolean bool
#define bool_true true
#define bool_false false

#endif /* SC_TYPES_H_ */
