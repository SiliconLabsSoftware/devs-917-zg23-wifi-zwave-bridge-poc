/*******************************************************************************
 * @file  sl_tcpip_handler.h
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

#ifndef APPS_THREADS_SL_TCPIP_HANDLER_H_
#define APPS_THREADS_SL_TCPIP_HANDLER_H_

void sl_tcpip_init(void);

sl_status_t zw_tcpip_post_event(uint32_t event, void *data);

#endif /* APPS_THREADS_SL_TCPIP_HANDLER_H_ */
