/*******************************************************************************
 * @file  sl_ipnode_utils.h
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
#ifndef UTLS_SL_IPNODE_UTILS_H_
#define UTLS_SL_IPNODE_UTILS_H_

#include <stdint.h>
#include "sl_uip_def.h"
#include "sl_tcpip_def.h"
#include "ZW_udp_server.h"

#define BKP_ZIP_SIZE  256

//#define UIP_IP_BUF                          ((struct uip_ip_hdr *)&uip_buf[UIP_LLH_LEN])
//#define UIP_ICMP_BUF                      ((struct uip_icmp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
//#define UIP_UDP_BUF                        ((struct uip_udp_hdr *)&uip_buf[uip_l2_l3_hdr_len])
//#define ZIP_PKT_BUF                      ((ZW_COMMAND_ZIP_PACKET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN])
//#define ZIP_PKT_BUF_SIZE                 400
//#define IP_ASSOC_PKT_BUF                 ((ZW_COMMAND_IP_ASSOCIATION_SET*)&uip_buf[UIP_LLH_LEN + UIP_IPUDPH_LEN + ZIP_HEADER_SIZE])
//#define BACKUP_UIP_IP_BUF                ((struct uip_ip_hdr *)backup_buf)
//#define BACKUP_IP_ASSOC_PKT_BUF          ((ZW_COMMAND_IP_ASSOCIATION_SET*)&backup_buf[UIP_IPUDPH_LEN + ZIP_HEADER_SIZE])
//#define BACKUP_PKT_BUF                   ((ZW_COMMAND_ZIP_PACKET*)&backup_buf[UIP_IPUDPH_LEN])

/* Header sizes. */
#if UIP_CONF_IPV6
#define UIP_IPH_LEN    40
#define UIP_FRAGH_LEN  8
#else /* UIP_CONF_IPV6 */
#define UIP_IPH_LEN    20    /* Size of IP header */
#endif /* UIP_CONF_IPV6 */

#define UIP_UDPH_LEN    8    /* Size of UDP header */
#define UIP_TCPH_LEN   20    /* Size of TCP header */
#ifdef UIP_IPH_LEN
#define UIP_ICMPH_LEN   4    /* Size of ICMP header */
#endif
#define UIP_IPUDPH_LEN (UIP_UDPH_LEN + UIP_IPH_LEN)    /* Size of IP +
                                                        * UDP
                                                        * header */
#define UIP_IPTCPH_LEN (UIP_TCPH_LEN + UIP_IPH_LEN)    /* Size of IP +
                                                        * TCP
                                                        * header */
#define UIP_TCPIP_HLEN UIP_IPTCPH_LEN
#define UIP_IPICMPH_LEN (UIP_IPH_LEN + UIP_ICMPH_LEN) /* size of ICMP
                                                       + IP header */
#define UIP_LLIPH_LEN (UIP_LLH_LEN + UIP_IPH_LEN)    /* size of L2
                                                      + IP header */

extern sl_tcpip_buf_t bkp_con;
extern char bkp_zip[BKP_ZIP_SIZE];
extern uint32_t bkp_zip_len;

#define sl_backup_uip_buf(buf)   do { bkp_con.zw_con = buf->zw_con; } while (0)
#define sl_uip_buf_src_addr()    bkp_con.zw_con.ripaddr
#define sl_uip_buf_src_port()    bkp_con.zw_con.rport
#define sl_uip_buf_dst_addr()    bkp_con.zw_con.lipaddr
#define sl_uip_buf_dst_port()    bkp_con.zw_con.lport
#define sl_uip_buf_lendpoint()   bkp_con.zw_con.lendpoint
#define sl_uip_buf_rendpoint()   bkp_con.zw_con.rendpoint
#define sl_uip_buf_flags0()      bkp_con.zw_con.rx_flags
#define sl_uip_buf_flags1()      bkp_con.zw_con.tx_flags
#define sl_uip_buf_get_conn()    bkp_con.zw_con.conn

#define ZIP_PKT_BUF              ((ZW_COMMAND_ZIP_PACKET*)&bkp_zip[0])
#define ZIP_PKT_BUF_SIZE                 400
#define sl_backup_zip_buf(buf, len)   do { \
    memcpy(bkp_zip, buf, len);             \
    bkp_zip_len = len;                     \
} while (0)
#define sl_backup_zip_len()      bkp_zip_len

uint8_t
is_local_address(uip_ipaddr_t *ip);

#endif /* UTLS_SL_IPNODE_UTILS_H_ */
