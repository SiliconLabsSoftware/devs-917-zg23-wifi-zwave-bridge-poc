/*******************************************************************************
 * @file  sl_uip.h
 * @brief Define common data structure for uIP
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

#ifndef SL_UIP_DEF_H
#define SL_UIP_DEF_H

#include <stdint.h>
#include "sl_ip_types.h"

#define UIP_PROTO_ICMP  1
#define UIP_PROTO_TCP   6
#define UIP_PROTO_UDP   17
#define UIP_PROTO_ICMP6 58

#define UIP_MAX_FRAME_BYTES 256

#define UIP_HTONS(a) a // ((a << 8) & 0xFF) | ((a >> 8) & 0xFF)  //htons(a)
#define UIP_HTONL(x) x
#define uip_htonl(x) x
#define uip_ntohs(x) x

#define swaps(a) (((a & 0xFF) << 8) ) | ((a >> 8) & 0xFF)

/**
 * Representation of an IP address.
 *
 */
typedef union uip_ip4addr_t {
  uint8_t  u8[4];     /* Initializer, must come first!!! */
  uint16_t u16[2];
} uip_ip4addr_t;

#if UIP_CONF_IPV6
typedef union uip_ip6addr_t {
  uint8_t  u8[16];      /* Initializer, must come first!!! */
  uint16_t u16[8];
} uip_ip6addr_t;
#endif

#if UIP_CONF_IPV6
typedef uip_ip6addr_t uip_ipaddr_t;
#else /* UIP_CONF_IPV6 */

typedef uip_ip4addr_t uip_ipaddr_t;
#endif /* UIP_CONF_IPV6 */

typedef struct {
  uint8_t addr[6];
} uip_lladdr_t;
/**
 * Representation of a uIP UDP connection.
 */
struct uip_udp_conn {
  uip_ipaddr_t sipaddr; /* ::0 == IF_ANY*/
  uip_ipaddr_t ripaddr;   /**< The IP address of the remote peer. */
  uint16_t lport;        /**< The local port number in network byte order. */
  uint16_t rport;        /**< The remote port number in network byte order. */
  uint8_t  ttl;          /**< Default time-to-live. */
};

/*
 * \param addr A pointer to a uip_ipaddr_t variable that will be
 * filled in with the IP address.
 *
 * \param addr0 The first octet of the IP address.
 * \param addr1 The second octet of the IP address.
 * \param addr2 The third octet of the IP address.
 * \param addr3 The forth octet of the IP address.
 *
 * \hideinitializer
 */
#define uip_ipaddr(addr, addr0, addr1, addr2, addr3) do { \
    (addr)->u8[0] = addr0;                                \
    (addr)->u8[1] = addr1;                                \
    (addr)->u8[2] = addr2;                                \
    (addr)->u8[3] = addr3;                                \
} while (0)

/**
 * Construct an IPv6 address from eight 16-bit words.
 *
 * This function constructs an IPv6 address.
 *
 * \hideinitializer
 */
#define uip_ip6addr(addr, addr0, addr1, addr2, addr3, addr4, addr5, addr6, addr7) do { \
    (addr)->u16[0] = UIP_HTONS(addr0);                                                 \
    (addr)->u16[1] = UIP_HTONS(addr1);                                                 \
    (addr)->u16[2] = UIP_HTONS(addr2);                                                 \
    (addr)->u16[3] = UIP_HTONS(addr3);                                                 \
    (addr)->u16[4] = UIP_HTONS(addr4);                                                 \
    (addr)->u16[5] = UIP_HTONS(addr5);                                                 \
    (addr)->u16[6] = UIP_HTONS(addr6);                                                 \
    (addr)->u16[7] = UIP_HTONS(addr7);                                                 \
} while (0)

/**
 * Construct an IPv6 address from eight 8-bit words.
 *
 * This function constructs an IPv6 address.
 *
 * \hideinitializer
 */
#define uip_ip6addr_u8(addr, addr0, addr1, addr2, addr3, addr4, addr5, addr6, addr7, addr8, addr9, addr10, addr11, addr12, addr13, addr14, addr15) do { \
    (addr)->u8[0] = addr0;                                                                                                                              \
    (addr)->u8[1] = addr1;                                                                                                                              \
    (addr)->u8[2] = addr2;                                                                                                                              \
    (addr)->u8[3] = addr3;                                                                                                                              \
    (addr)->u8[4] = addr4;                                                                                                                              \
    (addr)->u8[5] = addr5;                                                                                                                              \
    (addr)->u8[6] = addr6;                                                                                                                              \
    (addr)->u8[7] = addr7;                                                                                                                              \
    (addr)->u8[8] = addr8;                                                                                                                              \
    (addr)->u8[9] = addr9;                                                                                                                              \
    (addr)->u8[10] = addr10;                                                                                                                            \
    (addr)->u8[11] = addr11;                                                                                                                            \
    (addr)->u8[12] = addr12;                                                                                                                            \
    (addr)->u8[13] = addr13;                                                                                                                            \
    (addr)->u8[14] = addr14;                                                                                                                            \
    (addr)->u8[15] = addr15;                                                                                                                            \
} while (0)

/** \brief Is IPv6 address a the link local all-nodes multicast address */
#define uip_is_addr_linklocal_allnodes_mcast(a) \
  ((((a)->u8[0]) == 0xff)                       \
   && (((a)->u8[1]) == 0x02)                    \
   && (((a)->u16[1]) == 0)                      \
   && (((a)->u16[2]) == 0)                      \
   && (((a)->u16[3]) == 0)                      \
   && (((a)->u16[4]) == 0)                      \
   && (((a)->u16[5]) == 0)                      \
   && (((a)->u16[6]) == 0)                      \
   && (((a)->u8[14]) == 0)                      \
   && (((a)->u8[15]) == 0x01))

/** \brief Is IPv6 address a the link local all-routers multicast address */
#if IPV6
#define uip_is_addr_linklocal_allrouters_mcast(a) \
  ((((a)->u8[0]) == 0xff)                         \
   && (((a)->u8[1]) == 0x02)                      \
   && (((a)->u16[1]) == 0)                        \
   && (((a)->u16[2]) == 0)                        \
   && (((a)->u16[3]) == 0)                        \
   && (((a)->u16[4]) == 0)                        \
   && (((a)->u16[5]) == 0)                        \
   && (((a)->u16[6]) == 0)                        \
   && (((a)->u8[14]) == 0)                        \
   && (((a)->u8[15]) == 0x02))
#else
#define uip_is_addr_linklocal_allrouters_mcast(a) 0
#endif

#define uip_ipaddr_prefixcmp(addr1, addr2, length) (memcmp(addr1, addr2, length >> 3) == 0)

#define  uip_is_4to6_addr(a) \
  ((((a)->u16[0]) == 0)      \
   && (((a)->u16[1]) == 0)   \
   && (((a)->u16[2]) == 0)   \
   && (((a)->u16[3]) == 0)   \
   && (((a)->u16[4]) == 0)   \
   && (((a)->u16[5]) == 0xFFFF))

/**
 * Copy an IP address to another IP address.
 *
 * Copies an IP address from one place to another.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr1, ipaddr2;

   uip_ipaddr(&ipaddr1, 192,16,1,2);
   uip_ipaddr_copy(&ipaddr2, &ipaddr1);
   \endcode
 *
 * \param dest The destination for the copy.
 * \param src The source from where to copy.
 *
 * \hideinitializer
 */
#ifndef uip_ipaddr_copy
#define uip_ipaddr_copy(dest, src) (*(dest) = *(src))
#endif

/**
 * Compare two IP addresses
 *
 * Compares two IP addresses.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr1, ipaddr2;

   uip_ipaddr(&ipaddr1, 192,16,1,2);
   if(uip_ipaddr_cmp(&ipaddr2, &ipaddr1)) {
   printf("They are the same");
   }
   \endcode
 *
 * \param addr1 The first IP address.
 * \param addr2 The second IP address.
 *
 * \hideinitializer
 */
#if !UIP_CONF_IPV6
#define uip_ipaddr_cmp(addr1, addr2) ((addr1)->u16[0] == (addr2)->u16[0] \
                                      && (addr1)->u16[1] == (addr2)->u16[1])
#else /* !UIP_CONF_IPV6 */
#define uip_ipaddr_cmp(addr1, addr2) (memcmp(addr1, addr2, sizeof(uip_ip6addr_t)) == 0)
#endif /* !UIP_CONF_IPV6 */

/**
 * Compare two IP addresses with netmasks
 *
 * Compares two IP addresses with netmasks. The masks are used to mask
 * out the bits that are to be compared.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr1, ipaddr2, mask;

   uip_ipaddr(&mask, 255,255,255,0);
   uip_ipaddr(&ipaddr1, 192,16,1,2);
   uip_ipaddr(&ipaddr2, 192,16,1,3);
   if(uip_ipaddr_maskcmp(&ipaddr1, &ipaddr2, &mask)) {
   printf("They are the same");
   }
   \endcode
 *
 * \param addr1 The first IP address.
 * \param addr2 The second IP address.
 * \param mask The netmask.
 *
 * \hideinitializer
 */
#if !UIP_CONF_IPV6
#define uip_ipaddr_maskcmp(addr1, addr2, mask)           \
  (((((uint16_t *)addr1)[0] & ((uint16_t *)mask)[0])     \
    == (((uint16_t *)addr2)[0] & ((uint16_t *)mask)[0])) \
   && ((((uint16_t *)addr1)[1] & ((uint16_t *)mask)[1])  \
       == (((uint16_t *)addr2)[1] & ((uint16_t *)mask)[1])))
#else
#define uip_ipaddr_prefixcmp(addr1, addr2, length) (memcmp(addr1, addr2, length >> 3) == 0)
#endif

#define uip_ipaddr_maskcmp_u8(a1, a2, m) \
  ((a1[0] & m[0]) == (a2[0] & m[0]))     \
  && ((a1[0] & m[0]) == (a2[0] & m[0]))  \
  && ((a1[0] & m[0]) == (a2[0] & m[0]))  \
  && ((a1[0] & m[0]) == (a2[0] & m[0]))

/**
 * Check if an address is a broadcast address for a network.
 *
 * Checks if an address is the broadcast address for a network. The
 * network is defined by an IP address that is on the network and the
 * network's netmask.
 *
 * \param addr The IP address.
 * \param netaddr The network's IP address.
 * \param netmask The network's netmask.
 *
 * \hideinitializer
 */
#define uip_ipaddr_isbroadcast(addr, netaddr, netmask) \
  ((uip_ipaddr_t *)(addr)).u16 & ((uip_ipaddr_t *)(addr)).u16

/**
 * Mask out the network part of an IP address.
 *
 * Masks out the network part of an IP address, given the address and
 * the netmask.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr1, ipaddr2, netmask;

   uip_ipaddr(&ipaddr1, 192,16,1,2);
   uip_ipaddr(&netmask, 255,255,255,0);
   uip_ipaddr_mask(&ipaddr2, &ipaddr1, &netmask);
   \endcode
 *
 * In the example above, the variable "ipaddr2" will contain the IP
 * address 192.168.1.0.
 *
 * \param dest Where the result is to be placed.
 * \param src The IP address.
 * \param mask The netmask.
 *
 * \hideinitializer
 */
#define uip_ipaddr_mask(dest, src, mask) do {                             \
    ((uint16_t *)dest)[0] = ((uint16_t *)src)[0] & ((uint16_t *)mask)[0]; \
    ((uint16_t *)dest)[1] = ((uint16_t *)src)[1] & ((uint16_t *)mask)[1]; \
} while (0)

/**
 * Pick the first octet of an IP address.
 *
 * Picks out the first octet of an IP address.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr;
   uint8_t octet;

   uip_ipaddr(&ipaddr, 1,2,3,4);
   octet = uip_ipaddr1(&ipaddr);
   \endcode
 *
 * In the example above, the variable "octet" will contain the value 1.
 *
 * \hideinitializer
 */
#define uip_ipaddr1(addr) ((addr)->u8[0])

/**
 * Pick the second octet of an IP address.
 *
 * Picks out the second octet of an IP address.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr;
   uint8_t octet;

   uip_ipaddr(&ipaddr, 1,2,3,4);
   octet = uip_ipaddr2(&ipaddr);
   \endcode
 *
 * In the example above, the variable "octet" will contain the value 2.
 *
 * \hideinitializer
 */
#define uip_ipaddr2(addr) ((addr)->u8[1])

/**
 * Pick the third octet of an IP address.
 *
 * Picks out the third octet of an IP address.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr;
   uint8_t octet;

   uip_ipaddr(&ipaddr, 1,2,3,4);
   octet = uip_ipaddr3(&ipaddr);
   \endcode
 *
 * In the example above, the variable "octet" will contain the value 3.
 *
 * \hideinitializer
 */
#define uip_ipaddr3(addr) ((addr)->u8[2])

/**
 * Pick the fourth octet of an IP address.
 *
 * Picks out the fourth octet of an IP address.
 *
 * Example:
   \code
   uip_ipaddr_t ipaddr;
   uint8_t octet;

   uip_ipaddr(&ipaddr, 1,2,3,4);
   octet = uip_ipaddr4(&ipaddr);
   \endcode
 *
 * In the example above, the variable "octet" will contain the value 4.
 *
 * \hideinitializer
 */
#define uip_ipaddr4(addr) ((addr)->u8[3])

#endif // SL_UIP_DEF_H
