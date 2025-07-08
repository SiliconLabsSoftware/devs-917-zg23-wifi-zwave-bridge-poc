/***************************************************************************/ /**
 * @file CC_binary_switch.h
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

#ifndef SL_CC_BSWITCH_H
#define SL_CC_BSWITCH_H

/**
 * @brief Handles Z-Wave Binary Switch command class frames.
 *
 * This function processes incoming frames for the Binary Switch Command Class,
 * including SET and REPORT commands, and sends appropriate responses or logs state.
 *
 * @param c      Pointer to the Z-Wave connection structure.
 * @param frame  Pointer to the received frame buffer.
 * @param length Length of the received frame.
 * @return sl_command_handler_codes_t Status code indicating how the command was handled.
 */
sl_command_handler_codes_t sl_cc_bswitch_handler(zwave_connection_t *c,
                                                 uint8_t *frame, uint16_t length);

#endif // SL_CC_BSWITCH_H
