/***************************************************************************/ /**
 * @file sl_zw_send_request.h
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

#ifndef SL_ZW_SEND_REQUEST_H
#define SL_ZW_SEND_REQUEST_H

/**
 * Callback to  \ref sl_zw_send_request.
 *
 * \param txStatus see \ref ZW_SendData
 * \param rxStatus see \ref SerialAPI_Callbacks::ApplicationCommandHandler
 * \param pCmd pointer to received data, NULL if no response was received.
 * \param cmdLength length of received data 0, if no response was received.
 * \param user User defined argument, send with \ref sl_zw_send_request
 *
 * \return True if more responses are expected. False if none is expected
 * For example, a multi channel endpoint find expects more multi channel find report
 * frames in response if the device has a big number of endpoints which cannot
 * fit in on the Z-wave frame.
 */
typedef int( *ZW_SendRequst_Callback_t)(
  BYTE txStatus,
  BYTE rxStatus,
  ZW_APPLICATION_TX_BUFFER *pCmd,
  WORD cmdLength,
  void* user);

/**
 * Send a request to a node and trigger the callback once the
 * response is received.
 *
 * \param p See \ref sl_zw_send_data_appl
 * \param pData See \ref sl_zw_send_data_appl
 * \param dataLength See \ref sl_zw_send_data_appl
 * \param responseCmd Expected command to receive. The command class is derived from the first
 * byte of pData.
 * \param user User defined value which will be return in \ref ZW_SendRequst_Callback_t
 *
 * \return True if the command was sent.
 */
BYTE sl_zw_send_request(
  ts_param_t* p,
  BYTE *pData,
  BYTE  dataLength,
  BYTE  responseCmd,
  WORD  timeout,
  void* user,
  ZW_SendRequst_Callback_t callback);

/**
 * Application command handler to match incoming frames to pending requests.
 */
BOOL sl_send_request_appl_cmd_handler(ts_param_t* p,
                                      ZW_APPLICATION_TX_BUFFER *pCmd, WORD cmdLength);

/**
 * Initialize the sl_zw_send_request state machine.
 */
void sl_zw_send_request_init();

/*
 * Abort send request for particular nodeid.
 * This command is used when removing the node. All pending Send Requests to that node need to be aborted.
 */
void ZW_Abort_SendRequest(uint8_t nodeid);

#endif // SL_ZW_SEND_REQUEST_H
