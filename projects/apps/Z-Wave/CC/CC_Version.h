/***************************************************************************/ /**
 * @file sl_cc_version.h
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

#ifndef SL_CC_VERSION_H
#define SL_CC_VERSION_H

sl_command_handler_codes_t VersionHandler(zwave_connection_t *c,
                                          uint8_t *frame, uint16_t length);

#endif // SL_CC_VERSION_H
