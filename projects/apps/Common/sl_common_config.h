/***************************************************************************/ /**
 * @file sl_common_config.h
 * @brief common config function
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

#ifndef SL_COMMON_CONFIG_H_
#define SL_COMMON_CONFIG_H_

#include "sl_uip_def.h"

#define CLOCK_SECOND  1000
#define clock_seconds() 0

#define FW_VERSION_MAJOR 1
#define FW_VERSION_MINOR 0
#define FW_VERSION_PATCH 0

typedef struct router_config {
  uint8_t version[3];
  /**
   * Destination of unsolicitated ZW frames.
   * Configuration parameter ZipUnsolicitedDestinationIp6 in zipgateway.cfg.
   */
  uip_ip6addr_t unsolicited_dest;
  /**
   * Destination port for unsolicitated ZW frames.
   * Configuration parameter ZipUnsolicitedDestinationPort in zipgateway.cfg.
   */
  uint16_t unsolicited_port;

/**
 * Prefix of Z-Wave HAN (we used to call it pan :-) )
 *
 * Used to generate ip addresses for devices on the PAN side.
 * Copied from cfg_pan_prefix if that is set.
 * Otherwise its copied from the nvm_config.ula_pan_prefix.
 */
  uip_ip6addr_t pan_prefix;

  /**
   * Configuration parameter TunScript in zipgateway.cfg.
   */
  uip_ip6addr_t tun_prefix;
  /**
   * Configuration parameter ZipTunIp6PrefixLength in zipgateway.cfg.
   */
  uint8_t tun_prefix_length;

  /**
   * IPv6 address of Z/IP router LAN side.
   *
   * The active LAN address used by the gateway, its a copy of cfg_lan_addr or the nvm_config.ula_lan_addr
   */
  uip_ip6addr_t lan_addr;
  /**
   *
   * The active LAN prefix length.
   * Default 64.
   */
  uint8_t lan_prefix_length;

  /**
   * Configuration parameter ZipLanIp6 in zipgateway.cfg.
   * This is the full ip address which the gateway uses on the LAN. If this is set
   * to the zero address, the gateway will use a ULA address which is stored in the
   * nvm_config_t
   */
  uip_ip6addr_t cfg_lan_addr;   //Lan address from config file
  /**
   * Configuration parameter ZipPanIp6 in zipgateway.cfg.
   *
   * The IPv6 pan prefix, this the 64bit prefix for the PAN.
   * The pan address a created as pan_prefix::nodeID. If this the zero address the
   * gateway will use a the ula prefix stored in the nvm_config.
   */
  uip_ip6addr_t cfg_pan_prefix; //PAN address from config file

  /**
   * Configuration parameter ZipLanIp6PrefixLength in zipgateway.cfg.
   *
   * Must be 64.  Copied to lan_prefix_length before use.
   */
  uint8_t cfg_lan_prefix_length;  //Not used on ZIP_Gateway..always 64

  /**
   * IPv6 default gateway/next hop on the LAN side.
   * Configuration parameter ZipLanGw6 in zipgateway.cfg.
   *
   * This must be set, if the gateway needs to communcate with IPv6 nodes
   * beyond the LAN prefix. If this is zero, the gateway will only be able
   * to communicate with nodes on LAN prefix.
   */
  uip_ip6addr_t gw_addr;

  /**
   * Path to script that controls the "Node Identify" indicator LED.
   *
   * Configuration parameter ZipNodeIdentifyScript in zipgateway.cfg.
   * See CallExternalBlinker() in CC_indicator.c
   */
  const char *node_identify_script;

  /** Configuration parameter LogLevel in zipgateway.cfg.
   * Not used.
   */
  int log_level;

  /** Length of configuration parameter ExtraClasses in zipgateway.cfg.
   * Non-secure part only.
   */
  uint8_t extra_classes_len;
  /** Length of configuration parameter ExtraClasses in zipgateway.cfg.
   * Secure part only.
   */
  uint8_t sec_extra_classes_len;
  /** Configuration parameter ExtraClasses in zipgateway.cfg.
   *
   * Non-secure extra command classes supported by the gateway application.
   * Secure and non-secure classes are separated by the "marker" 0xF100.
   */
  uint8_t extra_classes[32];
  /** Configuration parameter ExtraClasses in zipgateway.cfg.
   *
   * Secure extra command classes supported by the gateway application.
   * Secure and non-secure classes are separated by the "marker" 0xF100.
   */
  uint8_t sec_extra_classes[32];

  /*
   * Portal url
   */

  /** Configuration parameter ZipPortal in zipgateway.cfg.
   * IP address of portal.
   */
  char portal_url[64];
  /** Configuration parameter ZipPortalPort in zipgateway.cfg.
   * Port number of portal.
   */
  uint16_t portal_portno;

  /** Configuration parameter DebugZipIp4Disable in zipgateway.cfg.
   * Experimental feature.
   */
  uint8_t ipv4disable;
  /** Command line parameter setting for debug purposes. */
  uint8_t clear_eeprom;
  /** Configuration parameter ZipClientKeySize  in zipgateway.cfg.
   * Default 1024.
   */
  uint16_t client_key_size;

  /** Configuration parameter ZipManufacturerID in zipgateway.cfg.
   *
   */
  uint16_t manufacturer_id;
  /** Configuration parameter ZipProductType in zipgateway.cfg.
   *
   */
  uint16_t product_type;
  /** Configuration parameter ZipProductID in zipgateway.cfg.
   *
   */
  uint16_t product_id;
  /** Configuration parameter ZipHardwareVersion in zipgateway.cfg.
   *
   */
  uint16_t hardware_version;
  /** Configuration parameter ZipDeviceID in zipgateway.cfg.
   *
   */
  char device_id[64];
  /** Length of configuration parameter ZipDeviceID in zipgateway.cfg.
   *
   */
  uint8_t device_id_len;

  //certs info
  /** Configuration parameter ZipCaCert in zipgateway.cfg.
   * Path to certificate.
   */
  const char *ca_cert;
  /** Configuration parameter ZipCert in zipgateway.cfg.
   * Path to certificate.
   */
  const char *cert;
  /** Configuration parameter ZipPrivKey in zipgateway.cfg.
   * Path to SSL/DTLS private key.
   */
  const char *priv_key;
  /** Configuration parameter ZipPSK in zipgateway.cfg.
   *
   * DTLS key
   */
  char psk[64];
  /** Length of configuration parameter ZipPSK in zipgateway.cfg.
   *
   */
  uint8_t psk_len;

  //obsolete
  const char* echd_key_file;

  /** Whether the bridge controller supports smart start.
   * Auto-detected on start up.
   * True when smart start is enabled.
   */
  int enable_smart_start;

  /** Configuration parameter ZWRFRegion in zipgateway.cfg.
   *
   * RF region selection.
   * Only applies to 700-series chip.
   */
  uint8_t rfregion;

  /** Configuration parameter NormalTxPowerLevel in zipgateway.cfg.
   *
   * Mark if powerlevel set in the zipgateway.cfg
   * Only applies to 700-series chip.
   */
  int is_powerlevel_set;

  /** Configuration parameters NormalTxPowerLevel, Measured0dBmPower
   * in zipgateway.cfg.
   *
   * TX powerlevel
   * Only applies to 700-series chip.
   */
  uint8_t tx_powerlevel;
  /** Configuration parameters MaxLRTxPowerLevel
   * in zipgateway.cfg.
   *
   * MAX LR TX powerlevel
   * Only applies to 700-series chip.
   */
  int16_t max_lr_tx_powerlevel;

  /** Configuration parameter MaxLRTxPowerLevel in zipgateway.cfg.
   *
   * Mark if max lr powerlevel set in the zipgateway.cfg
   * Only applies to 700-series  an 800-series chip.
   */
  int is_max_lr_powerlevel_set;

  /** Configuration parameter ZWLBT in zipgateway.cfg.
   *
   *  sets the LBT Threshold anytime ZIPGW resets the Z-Wave chip.
   *  ZW_SetListenBeforeTalkThreshold()
   */
  uint8_t zw_lbt;
} sl_router_config_t;

#define ZIPMAGIC 1645985

#endif /* SL_COMMON_CONFIG_H_ */
