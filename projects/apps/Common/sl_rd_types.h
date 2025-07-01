/*******************************************************************************
 * @file  RD_types.c
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

#ifndef RD_TYPES_H
#define RD_TYPES_H

#include <stdint.h>

typedef uint16_t nodeid_t;

/*************************************************************/
/*                          Version Probing                  */
/*************************************************************/

/** Probe CC Version State
 * \ingroup cc_version_probe
 */
typedef enum {
  /* Initial state for probing version. */
  PCV_IDLE,
  /* GW is sending VERSION_COMMAND_CLASS_GET and wait for cc_version_callback to store the version. */
  PCV_SEND_VERSION_CC_GET,
  /* Check if this is the last command class we have to ask. */
  PCV_LAST_REPORT,
  /* Basic CC version probing is done. Check if this is a Version V3 node. */
  PCV_CHECK_IF_V3,
  /* It's a V3 node. GW is sending VERSION_CAPABILITIES_GET and wait for
   * version_capabilities_callback. */
  PCV_SEND_VERSION_CAP_GET,
  /* The node supports ZWS. GW is sending VERSION_ZWAVE_SOFTWARE_GET and wait
   * for version_zwave_software_callback. */
  PCV_SEND_VERSION_ZWS_GET,
  /* Full version probing is done. Finalize/Clean up the probing. */
  PCV_VERSION_PROBE_DONE,
} sl_pcv_state_t;

typedef enum {
  PCV_EV_INITIAL,
  PCV_EV_START,
  PCV_EV_CC_PROBED,
  PCV_EV_CC_NOT_SUPPORT,
  PCV_EV_VERSION_CC_REPORT_RECV,
  PCV_EV_VERSION_CC_CALLBACK_FAIL,
  PCV_EV_NOT_LAST,
  PCV_EV_VERSION_CC_DONE,
  PCV_EV_IS_V3,
  PCV_EV_NOT_V3,
  PCV_EV_VERSION_CAP_REPORT_RECV,
  PCV_EV_VERSION_CAP_CALLBACK_FAIL,
  PCV_EV_CAP_PROBED,
  PCV_EV_VERSION_ZWS_REPORT_RECV,
  PCV_EV_VERSION_ZWS_CALLBACK_FAIL,
  PCV_EV_ZWS_PROBED,
  PCV_EV_ZWS_NOT_SUPPORT,
} sl_pcv_event_t;

/** Struct to form the list of controlled CC, including CC and its latest version */
typedef struct cc_version_pair {
  /** The command class identifier. */
  uint16_t command_class;
  /** The version supported by the node. */
  uint8_t version;
} cc_version_pair_t;

/* The callback type for probe_cc_version done
 *
 * \param user The user defined data
 * \param callback The status code, 0 indicating success
 */
typedef void (*_pcvs_callback)(void *user, uint8_t status_code);

typedef struct {
  /* The index for controlled_cc, mainly for looping over the CC */
  uint8_t probe_cc_idx;
  /* Global probing state */
  sl_pcv_state_t state;
  /* The callback will be lost after sending asynchronous sl_zw_send_request. Keep the callback here for going back */
  _pcvs_callback callback;
} sl_probe_cc_version_state_t;

#endif
