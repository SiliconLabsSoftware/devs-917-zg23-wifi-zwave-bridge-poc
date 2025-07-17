/*******************************************************************************
 * @file  ipv46_if_handler.h
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
#ifndef IPV46_IF_HANDLER_H_
#define IPV46_IF_HANDLER_H_

#include <stdint.h>

/** \defgroup ip46natdriver IPv4 to IPv6 NAT-component I/O functions.
 * \ingroup ip46nat
 *
 * This submodule is used from Contiki, almost directly on the LAN
 * interface.
 *
 * It maps incoming IPv4 packets to internal (IPv4-mapped) IPv6
 * packets and outgoing (IPv4-mapped) IPv6 packets to IPv4.
 *
 * It operates directly on Contiki's uip_buf.
 *
 * @{
 */

/**
 * Input of the NAT interface driver.  If the destination of the IP address is a NAT address,
 * this will translate the IPv4 packet in  uip_buf to a IPv6 packet,
 * If the address is not a NAT'ed address, this function will not change the uip_buf.
 *
 * If the destination address is the the uip_hostaddr and the destination
 * UDP port is the Z-Wave port, translation will be performed. Otherwise, it will not.
 */
void ipv46nat_interface_input();

/**
 * Translate the the packet in uip_buf from a IPv6 packet to a IPv4 packet if the packet
 * in uip_buf is a mapped IPv4 packet.
 *
 * This functions updates uip_buf and uip_len with the translated IPv4 packet.
 */
uint8_t ipv46nat_interface_output();

void ip4to6_addr(uip_ip6addr_t* dst, uip_ip4addr_t* src);
uint8_t is_4to6_addr(const uip_ip6addr_t* ip);

/** @} */

#endif
