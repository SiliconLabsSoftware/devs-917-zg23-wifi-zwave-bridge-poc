/***************************************************************************/ /**
 * @file sl_cc_irrigation.h
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

#ifndef SL_CC_IRRIGATION_H
#define SL_CC_IRRIGATION_H

/**
 * @brief Handles Z-Wave Irrigation command class frames.
 *
 * This function processes incoming frames for the Irrigation Command Class,
 * including SHUTOFF and RUN commands, and sends appropriate responses.
 *
 * @param c      Pointer to the Z-Wave connection structure.
 * @param frame  Pointer to the received frame buffer.
 * @param length Length of the received frame.
 * @return sl_command_handler_codes_t Status code indicating how the command was handled.
 */
sl_command_handler_codes_t sl_cc_irrigation_handler(zwave_connection_t *c,
                                                    uint8_t *frame, uint16_t length);

#endif // SL_CC_IRRIGATION_H
