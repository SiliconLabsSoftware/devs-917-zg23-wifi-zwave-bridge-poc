/***************************************************************************/ /**
 * @file sl_cc_handler.h
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

#ifndef SL_CC_HANDLER_H
#define SL_CC_HANDLER_H

#include <stdint.h>
#include "Net/ZW_udp_server.h"

typedef struct {
  /**
   * This is the function which will be executed when frame of the given command class is received.
   * The handler MUST return proper \ref sl_command_handler_codes_t code.
   *
   * \param connection  a zwave_connection_t structure, this has info about the transport properties of
   * this frame.
   * \param payload  The data payload  of this  frame.
   * \param len      The length of this frame
   */
  sl_command_handler_codes_t (*handler)(zwave_connection_t *, uint8_t* frame, uint16_t length); /// the command handler self

  /**
   * Initializer, this function initializes the command handler, ie. resetting state machines etc.
   */
  void (*init)(void);  /// Initialize the command handler
  uint16_t cmdClass;   /// command class that this handler implements
  uint8_t  version;    /// version of the implemented command class
  uint8_t  padding[3];    /// padding for having correct alignment on both 32 and 64bit platform
  security_scheme_t  minimal_scheme; ///the minimal security level which this command is supported on.
} command_handler_t;

/**
 * @brief Executes the command handler for a given Z-Wave connection.
 *
 * This function processes the payload of a Z-Wave frame and executes the
 * appropriate command handler.
 *
 * @param connection Pointer to the Z-Wave connection structure.
 * @param payload Pointer to the data payload of the frame.
 * @param len Length of the data payload.
 * @param bSupervisionUnwrapped Indicates if the supervision is unwrapped.
 * @return Returns a code indicating the result of the command handler execution.
 */
sl_command_handler_codes_t
sl_cc_handler_run(zwave_connection_t * connection,
                  uint8_t *payload,
                  uint16_t len,
                  uint8_t bSupervisionUnwrapped);

/**
 * @brief Retrieves the version of a command handler for a given security scheme and command class.
 *
 * @param scheme The security scheme.
 * @param cmdClass The command class.
 * @return The version of the command handler.
 */
uint8_t ZW_comamnd_handler_version_get(security_scheme_t scheme, uint16_t cmdClass);

#endif
