/***************************************************************************/ /**
 * @file sl_zw_router.c
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

#include <stdint.h>
#include "Common/sl_rd_types.h"
#include "Common/sl_common_log.h"
#include "Common/sl_gw_info.h"
#include "Common/sl_router_events.h"
#include "sl_status.h"

#include "utls/zgw_crc.h"
#include "utls/ipv6_utils.h"
#include "utls/sl_node_sec_flags.h"
#include "utls/RF_Region_Set_Validator.h"
#include "lib/random.h"
#include "modules/sl_rd_data_store.h"

#include "Z-Wave/include/ZW_classcmd.h"
#include "Z-Wave/include/ZW_classcmd_ex.h"

#include "transport/sl_ts_param.h"
#include "transport/sl_zw_send_request.h"
#include "transport/sl_zw_send_data.h"
#include "transport/sl_ts_common.h"

#include "ip_translate/sl_zw_resource.h"

#include "Z-Wave/CC/CC_NetworkManagement.h"
#include "Serialapi.h"
#include "ZW_basis_api.h"

#include "ip_bridge/sl_bridge.h"
#include "ip_bridge/sl_classic_zip_node.h"

#include "sl_ts_thread.h"
#include "sl_cc_handler.h"
#include "sl_security_layer.h"
#include "sl_serial_api_handler.h"
#include "sl_zw_router.h"

extern uint8_t suc_changed;
extern security_scheme_t net_scheme;

controller_role_t controller_role = CTRL_SUC;

ZW_APPLICATION_TX_BUFFER sl_zw_app_txbuf;
zgw_state_t zgw_state = ZGW_BOOTING;
BYTE MyNIF[0xff];
BYTE MyNIFLen = 0;
#define NIF     ((NODEINFO *) MyNIF)
#define CLASSES ((BYTE *) &MyNIF[sizeof(NODEINFO)])
#define ADD_COMMAND_CLASS(c)                  \
  {                                           \
    CLASSES[MyNIFLen - sizeof(NODEINFO)] = c; \
    MyNIFLen++;                               \
    ASSERT(MyNIFLen < sizeof(MyNIF));         \
  }

/**
 * Command classes which we always support non-secure
 */
const BYTE MyClasses[] = { COMMAND_CLASS_ZWAVEPLUS_INFO,
                           COMMAND_CLASS_TRANSPORT_SERVICE,
                           COMMAND_CLASS_CRC_16_ENCAP,
                           COMMAND_CLASS_APPLICATION_STATUS,
                           COMMAND_CLASS_SECURITY_2 };

const BYTE IpClasses[] = {
  COMMAND_CLASS_ZIP,
  COMMAND_CLASS_ZIP_ND,
};

void sl_application_cmd_zip_handler(ts_param_t *p,
                                    ZW_APPLICATION_TX_BUFFER *pCmd,
                                    uint16_t cmdLength)
{
  LOG_PRINTF("sl_application_cmd_zip_handler %d->%d %d bytes\n",
             (int) p->snode,
             (int) p->dnode,
             cmdLength);

  if (pCmd->ZW_Common.cmdClass == COMMAND_CLASS_FIRMWARE_UPDATE_MD) {
    // Reserved for future use.
  }

  switch (pCmd->ZW_Common.cmdClass) {
    case COMMAND_CLASS_CONTROLLER_REPLICATION:
      ZW_ReplicationReceiveComplete();
      break;
    case COMMAND_CLASS_CRC_16_ENCAP:
      if (pCmd->ZW_Common.cmd != CRC_16_ENCAP) {
        return;
      }
      if (p->scheme != NO_SCHEME) {
        WRN_PRINTF("Security encapsulated CRC16 frame received. Ignoring.\n");
        return;
      }

      if (zgw_crc16(CRC_INIT_VALUE, &((BYTE *) pCmd)[0], cmdLength) == 0) {
        p->scheme = USE_CRC16;
        sl_application_cmd_zip_handler(
          p,
          (ZW_APPLICATION_TX_BUFFER *) ((BYTE *) pCmd + 2),
          cmdLength - 4);
      } else {
        ERR_PRINTF("CRC16 Checksum failed\n");
      }
      return;
    case COMMAND_CLASS_SECURITY:
      if (isNodeBad(p->snode)) {
        WRN_PRINTF("Dropping security0 package from KNOWN BAD NODE\n");
        return;
      }

      if (pCmd->ZW_Common.cmd != SECURITY_COMMANDS_SUPPORTED_REPORT) {
        security_CommandHandler(
          p,
          pCmd,
          (BYTE)cmdLength);   /* IN Number of command bytes including the command */
        return;
      }
      break;
    case COMMAND_CLASS_SECURITY_2:
      WRN_PRINTF("This version don't support S2\n");
      break;
    case COMMAND_CLASS_TRANSPORT_SERVICE:
      WRN_PRINTF("This version don't support transport class\n");
      return;
    case COMMAND_CLASS_MULTI_CHANNEL_V2:
      WRN_PRINTF("This version don't support multi v2 class\n");
      return;
    default:
      break;
  } // end case
  sl_zw_send_data_appl_rx_notify(p, (const uint8_t *) pCmd, cmdLength);
  // process data from request command.
  if (sl_send_request_appl_cmd_handler(p, pCmd, cmdLength)) {
    return;
  }

  zwave_connection_t c;
  memset(&c, 0, sizeof(c));

  /*
     Note: the lipaddr address is set back to ripaddr in ClassicZIPNode_SendUnsolicited()
     for sending unsol packets. Because ZIP Gateway is forwarding the packet */

  sl_ip_of_node(&c.ripaddr, p->snode);
  sl_ip_of_node(&c.lipaddr, p->dnode);
  /* Check if command should be handled by the Z/IP router itself */
  c.scheme    = p->scheme;
  c.rendpoint = p->sendpoint;
  c.lendpoint = p->dendpoint;
  c.rx_flags  = p->rx_flags;

  if (c.lendpoint == 0) {
    // Block access to Network Management CCs from RF
    if (pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_BASIC
        || pCmd->ZW_Common.cmdClass
        == COMMAND_CLASS_NETWORK_MANAGEMENT_INCLUSION
        || pCmd->ZW_Common.cmdClass
        == COMMAND_CLASS_NETWORK_MANAGEMENT_INSTALLATION_MAINTENANCE
        || pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_PRIMARY
        || pCmd->ZW_Common.cmdClass == COMMAND_CLASS_NETWORK_MANAGEMENT_PROXY) {
      WRN_PRINTF("Blocking RF access to Network Management CC\n");
      return;
    }

    sl_application_cmd_ip_handler(&c, (BYTE *) pCmd, cmdLength);
  } else {
    WRN_PRINTF("multi channel encap command to non-zero endpoint of gateway? "
               "Dropping.\n");
  }
}

/**
 * Input for RAW Z-Wave frames
 */
void sl_application_serial_handler(uint8_t rxStatus,
                                   nodeid_t destNode,
                                   nodeid_t sourceNode,
                                   ZW_APPLICATION_TX_BUFFER *pCmd,
                                   uint8_t cmdLength)
{
  ts_param_t p;
  p.dendpoint = 0;
  p.sendpoint = 0;

  p.rx_flags = rxStatus;
  p.tx_flags =
    ((rxStatus & RECEIVE_STATUS_LOW_POWER) ? TRANSMIT_OPTION_LOW_POWER : 0)
    | TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_EXPLORE
    | TRANSMIT_OPTION_AUTO_ROUTE;

  if (nodemask_nodeid_is_invalid(sourceNode)) {
    WRN_PRINTF("source node id: %d is out of range. Dropping!\n", sourceNode);
    return;
  }

  if ((destNode != 0xff) && (p.rx_flags & RECEIVE_STATUS_TYPE_EXPLORE)) {
    DBG_PRINTF(
      "Destination node is NOT 0xff and RECEIVE_STATUS_TYPE_EXPLORE is "
      "set. Marking the frame as neither MCAST nor BCAST. \n");
    p.rx_flags &= ~(RECEIVE_STATUS_TYPE_MULTI | RECEIVE_STATUS_TYPE_BROAD);
  }

  DBG_PRINTF(
    "serial update node, dnode: %d, mynode: %d, snode: %d, status: %x\n",
    destNode,
    MyNodeID,
    sourceNode,
    rxStatus);
  p.scheme = NO_SCHEME;
  p.dnode  = destNode ? destNode : MyNodeID;
  p.snode  = sourceNode;

  sl_application_cmd_zip_handler(&p, pCmd, cmdLength);
}

/**
 * Main application command handler for commands coming both via Z-Wave and IP
 */
bool sl_application_cmd_ip_handler(zwave_connection_t *c,
                                   void *pData,
                                   u16_t bDatalen)
{
  ZW_APPLICATION_TX_BUFFER *pCmd = (ZW_APPLICATION_TX_BUFFER *) pData;
  c->tx_flags =
    ((c->rx_flags & RECEIVE_STATUS_LOW_POWER) ? TRANSMIT_OPTION_LOW_POWER
     : 0)
    | TRANSMIT_OPTION_ACK | TRANSMIT_OPTION_EXPLORE
    | TRANSMIT_OPTION_AUTO_ROUTE;

  if (bDatalen < 2) {
    ERR_PRINTF("sl_application_cmd_ip_handler: Package is too small.\r\n");
    return true;
  }

  // The wake command class is a controlling command, it will receive
  // special attention.
  if (pCmd->ZW_NodeInfoCachedGetFrame.cmdClass == COMMAND_CLASS_WAKE_UP) {
    ERR_PRINTF("sl_application_cmd_ip_handler: don't support wake up command\n");
    return true;
  }

  if (pCmd->ZW_SupervisionGetFrame.cmdClass == COMMAND_CLASS_SUPERVISION
      && pCmd->ZW_SupervisionGetFrame.cmd == SUPERVISION_GET) {
    /* Always allow, whether or not it's multicast */
  } else if ((c->rx_flags & RECEIVE_STATUS_TYPE_MASK)
             != RECEIVE_STATUS_TYPE_SINGLE) {
    /* Drop all multicast frames except for node info cached get from IP */
    goto send_to_unsolicited;
  }
// DS
#if (NM_NOTITY)
  // notify to network manage.
  NetworkManagement_frame_notify();
#endif

  // FALSE since we are not unwrapping supervision and CC_Supervision will set
  // this flag.
  int rc = sl_cc_handler_run(c, pData, bDatalen, false);
  if (rc != COMMAND_HANDLED) {
    DBG_PRINTF(
      "sl_application_cmd_ip_handler: Unhandled command  %02x:%02x from ",
      pCmd->ZW_Common.cmdClass,
      pCmd->ZW_Common.cmd);
    uip_debug_ipaddr_print(&c->ripaddr);
    DBG_PRINTF("\n");
  }
  return true;

  send_to_unsolicited:
  DBG_PRINTF("sl_application_cmd_ip_handler: don't support send solicited\n");

  return true;
}

void sl_serial_api_started(uint8_t *pData, uint8_t length)
{
  (void)pData; // Unused parameter
  (void)length; // Unused parameter
  LOG_PRINTF("SerialAPI restarted\n");
  if (ZW_RFRegionGet() == RF_US_LR) {
    if (SerialAPI_EnableLR() == false) {
      LOG_PRINTF("Fail to enable Z-Wave Long Range capability\n");
    }
  } else {
    if (SerialAPI_DisableLR() == false) {
      LOG_PRINTF("Fail to disable Z-Wave Long Range capability\n");
    }
  }
}

void sl_appl_controller_update(
  BYTE bStatus,       /**<  Status event */
  nodeid_t bNodeID,   /**<  Node id of the node that send node info */
  BYTE *pCmd,         /**<  Pointer to Application Node information */
  BYTE bLen,          /**<  Node info length                        */
  BYTE *prospectHomeID   /**< NULL or the prospect homeid if smart start inclusion */
  )
{
  (void)prospectHomeID; // Unused parameter
  LOG_PRINTF("sl_appl_controller_update: status=0x%x node=%d NIF len=%u\n",
             (unsigned) bStatus,
             bNodeID,
             bLen);

  if ((bStatus == UPDATE_STATE_NODE_INFO_FOREIGN_HOMEID_RECEIVED)
      || (bStatus == UPDATE_STATE_NODE_INFO_SMARTSTART_HOMEID_RECEIVED_LR)) {
    /* This bStatus has different syntax from others, so handle this first */
    /* pCmd is first part of the public key-derived homeID */
    return;
  } else if (bStatus == UPDATE_STATE_INCLUDED_NODE_INFO_RECEIVED) {
    uint8_t INIF_rxStatus;
    uint8_t INIF_NWI_homeid[4];

    INIF_rxStatus = pCmd[0];
    memcpy(INIF_NWI_homeid, &pCmd[1], 4);
    sl_nm_inif_received(bNodeID, INIF_rxStatus, INIF_NWI_homeid);
    return;
  }

  if (nodemask_nodeid_is_invalid(bNodeID)
      && (bStatus != UPDATE_STATE_NODE_INFO_REQ_FAILED)) {
    ERR_PRINTF("Controller update from invalid nodeID %d", bNodeID);
    return;
  }

  switch (bStatus) {
    case UPDATE_STATE_NEW_ID_ASSIGNED:
      /**
       * We start a timer here to wait for the inclusion controller to probe the node, If
       * we get an COMMAND_CLASS_INCLUSION_CONTROLLER INITIATE command, then we stop this timer.
       */
      break;
    case UPDATE_STATE_NODE_INFO_RECEIVED:
      // Reserved for future use.
      break;
    case UPDATE_STATE_NODE_INFO_REQ_DONE:
      // Reserved for future use.
      break;
    case UPDATE_STATE_NODE_INFO_REQ_FAILED:
      // Reserved for future use.
      break;
    case UPDATE_STATE_ROUTING_PENDING:
      // Reserved for future use.
      break;
    case UPDATE_STATE_DELETE_DONE:
      // Reserved for future use.
      break;
    case UPDATE_STATE_SUC_ID:
      DBG_PRINTF("SUC node Id updated, new ID is %i...\n", bNodeID);
      break;
    default:
      DBG_PRINTF("sl_appl_controller_update: Unknown status %d\n", bStatus);
      break;
  }
}

/**
 * Setup the GW NIF
 */
BYTE sl_application_nif_init(void)
{ /* IN   Nothing   */
  BYTE ver;
  BYTE capabilities;
  BYTE len;
  BYTE chip_type;
  BYTE chip_version;
  BYTE nodelist[32];
  BYTE cap;
  nodeid_t n;
  DWORD tmpHome; //Not using homeID science it might have side effects
                 //  int i;
  sl_zw_get_node_proto_info(MyNodeID, NIF);
  NIF->nodeType.specific = SPECIFIC_TYPE_GATEWAY;
  NIF->nodeType.generic  = GENERIC_TYPE_STATIC_CONTROLLER;
  memcpy(CLASSES, MyClasses, sizeof(MyClasses));
  MyNIFLen = sizeof(MyClasses) + sizeof(NODEINFO);

  SerialAPI_GetInitData(&ver,
                        &capabilities,
                        &len,
                        nodelist,
                        &chip_type,
                        &chip_version);
  // NOTE: No need of getting LR node list here as we dont really need it
  // Even classic nodelist above is not used anywhere in this function

  LOG_PRINTF("%u00 series chip version %u serial api version %u\n",
             chip_type,
             chip_version,
             ver);

  router_cfg.enable_smart_start = 0;

  cap = ZW_GetControllerCapabilities();

  if ((cap & (CONTROLLER_NODEID_SERVER_PRESENT | CONTROLLER_IS_SECONDARY))
      == 0) {
    MemoryGetID((BYTE *) &tmpHome, &n);
    LOG_PRINTF("Assigning myself(NodeID %u ) SIS role \n", n);
    ZW_SetSUCNodeID(n, TRUE, FALSE, ZW_SUC_FUNC_NODEID_SERVER, 0);
    cap         = ZW_GetControllerCapabilities();
    suc_changed = 1;
  }

  if ((cap & CONTROLLER_NODEID_SERVER_PRESENT) == 0
      || (cap & CONTROLLER_IS_SUC) == 0) {
    router_cfg.enable_smart_start = 0;
    WRN_PRINTF("SMART START is disabled because the controller is not SIS\n");
  }

  if (capabilities & GET_INIT_DATA_FLAG_SECONDARY_CTRL) {
    LOG_PRINTF("I'am a Secondary controller\n");
    controller_role = CTRL_SECONDARY;
  }
  if (capabilities & GET_INIT_DATA_FLAG_IS_SUC) {
    LOG_PRINTF("I'am SUC\n");
    controller_role = CTRL_SUC;
  }
  if (capabilities & GET_INIT_DATA_FLAG_SLAVE_API) {
    LOG_PRINTF("I'am slave\n");
    controller_role = CTRL_SLAVE;
  }

  MemoryGetID((BYTE *) &homeID, &MyNodeID);
  security_init();

  uint8_t flags = NODE_FLAG_SECURITY0;
  net_scheme    = NO_SCHEME;

  if (flags & NODE_FLAG_SECURITY0) {
    /*Security 0 should only go to the NIF if we have S0 key*/
    ADD_COMMAND_CLASS(COMMAND_CLASS_SECURITY);
    net_scheme = SECURITY_SCHEME_0;
  }
#if S2_SUPPORTED
  if (flags & NODE_FLAG_SECURITY2_UNAUTHENTICATED) {
    net_scheme = SECURITY_SCHEME_2_UNAUTHENTICATED;
  }
  if (flags & NODE_FLAG_SECURITY2_AUTHENTICATED) {
    net_scheme = SECURITY_SCHEME_2_AUTHENTICATED;
  }
  if (flags & NODE_FLAG_SECURITY2_ACCESS) {
    net_scheme = SECURITY_SCHEME_2_ACCESS;
  }
#endif

  LOG_PRINTF("Network scheme is: %s\n", network_scheme_name(net_scheme));
  if (cap & CONTROLLER_NODEID_SERVER_PRESENT) {
    LOG_PRINTF("I'm a primary or inclusion controller.\n");
  } else if ((cap & CONTROLLER_IS_SECONDARY) == 0) {
    DBG_PRINTF("I'am a Primary controller\n");
  }
  return 0;
}

BOOL sl_zip_router_reset()
{
  nm_state_t nms = sl_nm_get_state();

  LOG_PRINTF("Resetting ZIP Gateway\n");

  if (nms == NM_WAIT_FOR_MDNS || nms == NM_SET_DEFAULT) {
    /* There is already a network management operation that will
     * trigger a reset, so just hang in there. */
    LOG_PRINTF("Await pending reset in Network Management.\n");
    return TRUE;
  }

  MemoryGetID((BYTE *) &homeID, &MyNodeID);
  const uint16_t magic_rand = 0x1256;
  /*Seed the random number generator*/
  random_init(magic_rand);

  DBG_PRINTF(
    "...... Firmware Update Meta Data command class version: %i ......\n",
    ZW_comamnd_handler_version_get(TRUE, COMMAND_CLASS_FIRMWARE_UPDATE_MD));

#ifdef TEST_MULTICAST_TX
  /* Flush the multicast groups. */
  mcast_group_init();
#endif

  sl_application_nif_init();

#if TRANSPORT_SERVER_SUPPORTED
  ZW_TransportService_Init(sl_application_cmd_zip_handler);
#endif
  sl_zw_send_request_init();

  /* init: at this point ZW_Send_* functions can be used */
  if (!data_store_init()) {
    LOG_PRINTF("INIT data_store_init fail!\n");
    return FALSE;
  }
#if MB_SUPPORTED
  mb_init();
#endif

  /*Network management is initialized async*/
  network_management_init_done = 0;

  uint8_t rfregion;
  uint8_t max_idx;
  if (ZW_GECKO_CHIP_TYPE(chip_desc.my_chip_type)) {
    rfregion = ZW_RFRegionGet();
  } else {
    rfregion = router_cfg.rfregion; // ZW_RFRegionGet() is not available in 500
  }

  rfregion = RF_REGION_CHECK(rfregion);
  if (rfregion != 0xFE) {
    if ((rfregion == ZW_JP) || (rfregion == ZW_KR)) {
      max_idx = 3;
    } else {
      max_idx = 2;
    }
    for (uint8_t channel_idx = 0; channel_idx < max_idx; channel_idx++) {
      if (!ZW_SetListenBeforeTalkThreshold(channel_idx, router_cfg.zw_lbt)) {
        ERR_PRINTF("ERROR: Failed Setting ListenBeforeTalk Threshold to %d on "
                   "channel: %d\n",
                   router_cfg.zw_lbt,
                   channel_idx);
        goto err;
      }
      DBG_PRINTF("Setting ListenBeforeTalk Threshold to %d channel: %d\n",
                 router_cfg.zw_lbt,
                 channel_idx);
    }
  }
  err:
  return TRUE;
}

void zgw_component_done(zgw_state_t comp, void *data)
{
  (void)data; // Unused parameter
  zgw_state &= ~comp;
}

void zgw_component_start(zgw_state_t comp)
{
  zgw_state |= comp;
}

/*---------------------------------------------------------------------------*/
/**
 * \fn zip_process
 *
 * \ingroup ZIP_Router
 *
 * Main Z/IP router process
 *
 * This process starts all other required processes of the gateway.

   ZIP Router Init Procedure
   --------------------------
   Outline of the init procedure used on startup and setdefault of zip router family.

   Full inits are triggered by either receiving a Network Management #DEFAULT_SET command
   or by starting the ZIP Router. In the latter case, sl_zip_router_reset() is called directly.

   Full inits will also be triggered from \ref NW_CMD_handler by GW learn mode.

   When triggered from NMS, an init must be preceded by a teardown,
   invalidation, or rediscovery of some components.


   Initialization Steps
   --------------------

   These steps also apply when re-initializating after a teardown.

   - When #zip_process receives #ZIP_EVENT_RESET
   - #sl_zip_router_reset();
   - stop/start serial_api_process
   - read in #homeID/#MyNodeID from serial
   - init random
   - init eeprom
   - init the zipgateway's cache of the serial device's capabilities and the zipgateway NIF
   - init ipv6
   - stop/start tcpip_process  ---> will emit \ref tcpip_event when done
   - stop/start udp_server_process, dtls_server_process
   - start \ref ZIP_MDNS (if it is not already running)
   - init ipv4 (if it is not already initialized)
   - init node queue
   - \ref ZW_TransportService_Init (sl_application_cmd_zip_handler)
   - initialize \ref Send_Request (at this point ZW_Send_* functions can be used)
   - init data store file - if #homeID or #MyNodeID have changed, update file and invalidate bridge.
   - init \ref mailbox
   - stop/start \ref ZIP_DHCP ---> will emit #ZIP_EVENT_ALL_IPV4_ASSIGNED
   - init provisioning list
   - set NM init "not done"

   - When #zip_process receives tcpip_event
   - #sl_class_zip_node_init();
   - #bridge_init() ---> will emit #ZIP_EVENT_BRIDGE_INITIALIZED

   - When #zip_process receives #ZIP_EVENT_BRIDGE_INITIALIZED
   - #ApplicationInitProtocols()
   - sl_zw_send_data_appl_init();
   - unlock RD: #rd_init (FALSE); ---> will emit #ZIP_EVENT_ALL_NODES_PROBED
   - import from eeprom file, reset #nat_table, update rd/nat_table if needed, send DHCP DISCOVER for gw, resume rd probe.
   - #rd_probe_new_nodes(); -- This looks redundant
   - sl_nm_init();
   - cancel mns timer, set NM init "done", clear waiting_for_middleware_probe flag
   - #send_nodelist() if this was requested before reset and NMS is IDLE
   - #sl_nm_net_update_status() ---> may emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - if all flags are in:
   - send the prepared NMS reply buffer ---> will emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - reset NMS to #NM_IDLE

   - When #zip_process receives #ZIP_EVENT_ALL_NODES_PROBED
   - NetworkManagement_all_nodes_probed()
   - sl_nm_net_update_status() ---> may emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - if all flags are in:
   - send the prepared NMS reply buffer ---> will emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - set NMS state to #NM_IDLE
   - if DHCP is done and NMS is IDLE
   - #send_nodelist() if this was requested before reset
   - post #ZIP_EVENT_QUEUE_UPDATED

   - When #zip_process receives #ZIP_EVENT_ALL_IPV4_ASSIGNED
   - if probing is done  and NMS is IDLE
   - #send_nodelist() if this was requested before reset
   - sl_nm_net_update_status(); ---> may emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - if all flags are in:
   - send the prepared NMS reply buffer ---> will emit #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - set NMS state to #NM_IDLE

   - When #zip_process receives #ZIP_EVENT_NETWORK_MANAGEMENT_DONE
   - #send_nodelist() if this was requested before reset and NMS is IDLE or if #zip_process just starts
   - process the node queues

   - When #zip_process receives #ZIP_EVENT_QUEUE_UPDATED
   - process the node queues


   Tear down in Learn Mode Scenarios
   ---------------------------------

 \<NetworkManagement receives #LEARN_MODE_SET CMD>
   - #ZW_SetLearnMode(.., \<LearnModeStatus callback>)
   - #LearnModeStatus (#LEARN_MODE_STARTED)
   - lock RD, invalidate #MyNodeID, start sec2 learn mode with #SecureInclusionDone() callback
   - LearnModeStatus(#LEARN_MODE_DONE)
   - if exclusion:
   - exit DHCP and send DHCP release on nodeid 1, if this was not GW
   - Set #MyNodeID=1 and update nat_table entry of GW to nodeid 1
   - prepare nms reply
   - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)
   - set NMS in #NM_SET_DEFAULT
   - start DHCP
   - if inclusion:
   - set NMS in state #NM_WAIT_FOR_SECURE_LEARN
   - use the new nodeid of the gateway to delete any existing nat_table entry for that nodeid and update the nat_table entry of the GW - the ipv4 address of the gateway is retained
   - exit DHCP process
   - delete the RD entry of the new gateway nodeid
   - update #MyNodeID/#homeID over serial api, refresh ipv6 addresses and sec2 with this
   - #ipv46nat_del_all_nodes() - delete nodes in the old network from nat_table (and send DHCP release)
   - if interview_completed requested
    - start DHCP and add including node to nat_table
   - #new_pan_ula()
   - wait for security completion on PAN side (#SecureInclusionDone() callback)
   - if ctrl replication/ctrl shift
   - restore #MyNodeID/#homeID
   - prepare nms reply
   - set NMS in state #NM_WAIT_FOR_MDNS
   - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)

   For inclusion of the gateway, this happens asynchronously:

   - #SecureInclusionDone(#NM_EV_SECURITY_DONE) in #NM_WAIT_FOR_SECURE_LEARN
   - prepare nms reply
   - set NMS in #NM_WAIT_FOR_MDNS
   - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)

   Tear down then continues:

   - When #zip_process receives PROCESS_EVENT_EXITED(mDNS_server_process)
   - rd_destroy()
   - call #NetworkManagement_mdns_exited()

   At this point, Network Management can be in #NM_SET_DEFAULT state (in
   case of exclusion) or #NM_WAIT_FOR_MDNS state (any other outcome).

   - NetworkManagement_mdns_exited() in #NM_WAIT_FOR_MDNS
   - if incl or excl, do bridge_reset()
   - if interview_completed requested
   - send nms reply
   - setup nms interview_completed reply
   - setup timer and wait for interview by SIS
   - when interview is completed,  --->  emit #ZIP_EVENT_RESET
    - set NMS state to #NM_WAIT_FOR_OUR_PROBE
    - (when our (post-reset) interview is completed (receive #NM_EV_ALL_PROBED)
     - unlock RD
     - start nms timer)
   - else  --->  emit #ZIP_EVENT_RESET
   - unlock RD
   - start reply timer

   In the #NM_SET_DEFAULT scenarios, tear down continues as follows

   - #NetworkManagement_mdns_exited() in #NM_SET_DEFAULT ---> will emit #ZIP_EVENT_RESET
   - ApplicationDefaultSet()
   - #rd_exit() -- again, but here it is almost certainly a NOP
   - #ipv46nat_del_all_nodes() - delete nodes in the old network from nat_table (and send DHCP release)
   - #rd_data_store_invalidate(), read #homeID/#MyNodeID
   - ZW_SetSUCNodeID(#MyNodeID,...)
   - #security_set_default();
   - #sec2_create_new_network_keys();
   - #bridge_reset() -- invalidate bridge
   - #new_pan_ula()
   - process_post(&zip_process, #ZIP_EVENT_RESET, 0);
   - change NMS state to #NM_WAITING_FOR_PROBE
   - start nms timer
   - re-start probing:  #rd_probe_lock (FALSE) - this should pause, waiting for bridge

   From here on, the gateway continues with the steps from
   initialization.

   Tear down in Set Default scenario
   ---------------------------------

 \<NetworkManagement receives #DEFAULT_SET CMD>
   - \ref ZW_SetDefault(\<SetDefaultStatus-callback>)
   - set NMS state to #NM_SET_DEFAULT
   - SetDefaultStatus()
   - call rd_exit() to tear down mDNS and RD ---> will emit PROCESS_EVENT_EXITED(mDNS_server_process)

   From here on, teardown continues as in the learn mode exclusion
   scenario from where #zip_process receives
   PROCESS_EVENT_EXITED(mDNS_server_process).


   Tear down in Set-as-SIS Scenario
   --------------------------------

 \<#sl_appl_controller_update() receives #UPDATE_STATE_SUC_ID>
   - set flag to send new nodelist to unsolicited after reset
   - if the new id is #MyNodeID, post #ZIP_EVENT_RESET to #zip_process

   From here on, the gateway continues with the steps from
   initialization.


 */

static osMessageQueueId_t sli_zip_queue;

/*===========================================================================*/
/**
 * @brief .
 *
 * @param[in] none
 * @return void
 * @note
 */
sl_status_t zw_zip_post_event(uint32_t event, void *data)
{
  sl_cc_net_ev_t msg = { .ev = event, .ev_data = data };

  osMessageQueuePut(sli_zip_queue, (void *) &msg, 0, osWaitForever);
  return SL_STATUS_OK;
}

/*===========================================================================*/
/**
 * @brief .
 *
 * @param[in] none
 * @return void
 * @note
 */
sl_status_t zw_zip_get_event(void *msg, uint32_t timeout)
{
  if (osMessageQueueGet(sli_zip_queue, msg, NULL, timeout) == osOK) {
    return SL_STATUS_OK;
  }
  return SL_STATUS_TIMEOUT;
}

void sl_router_process(void)
{
  sl_cc_net_ev_t msg;
  if (zw_zip_get_event(&msg, 5) == osOK) {
    printf("zw_zip_get_event: ev = %ld\n", msg.ev);
    uint32_t ev = msg.ev;
    void *data  = msg.ev_data;
    if (ev == ZIP_EVENT_NODE_IPV4_ASSIGNED) {
      nodeid_t node = ((uintptr_t) msg.ev_data) & 0xFFFF;
      if (node == MyNodeID) {
        /*Now the that the Z/IP gateway has an IPv4 address we may route IPv4 to IPv6 mapped addresses to the gateway itself.*/

        /* With this new route it might be possible to for the NM module to deliver the reply package after a Set default
         * or learn mode. */
        sl_nm_init();
      } else {
        /* Notify network management module that we have a new IPv4 address. */
      }
    } else if (ev == ZIP_EVENT_RESET) {
      // Reserved for future use.
    } else if (ev == ZIP_EVENT_BRIDGE_INITIALIZED) {
      zgw_component_done(ZGW_BRIDGE, data);

      // DS
#if S2_SUPPORTED
      /* Unpersist S2 SPAN table after ApplicationInitProtocols,
       * as that initialize Resource Directory, which holds the persisted S2 SPAN table*/
      sec2_unpersist_span_table();
#endif
      /* With this IPv6 ready it might be possible to for the NM
       * module to deliver the reply package after a Set default or
       * learn mode. */
      sl_nm_init();

      /* Tell NM to poll for its requirements.  This will set
      * NETWORK_UPDATE_FLAG_VIRTUAL and maybe other flags. */
      sl_nm_net_update_status(0);
    } else if (ev == ZIP_EVENT_NETWORK_MANAGEMENT_DONE) {
      /* Always send node list report when GW just starts */
      if (sl_nm_is_idle()) {
        /* If NMS is not waiting for a middleware probe, it is done
         * now. */
        zgw_component_done(ZGW_NM, data);
      }
    } else if (ev == ZIP_EVENT_NM_VIRT_NODE_REMOVE_DONE) {
      sl_nm_virtual_nodes_removed();
    } else if (ev == ZIP_EVENT_BACKUP_REQUEST) {
      zgw_component_start(ZGW_BU);
    } else if (ev == ZIP_EVENT_COMPONENT_DONE) {
      // Reserved for future use.
    }
  }
}

void sl_router_init(void)
{
  sli_zip_queue = osMessageQueueNew(10, sizeof(sl_cc_net_ev_t), NULL);
  sl_config_init();
  LOG_PRINTF("\nTunnel prefix       ");
  uip_debug_ipaddr_print(&router_cfg.tun_prefix);
  LOG_PRINTF("\nLan address         ");
  uip_debug_ipaddr_print(&router_cfg.lan_addr);
  LOG_PRINTF("\nHan address         ");
  uip_debug_ipaddr_print(&router_cfg.pan_prefix);
  LOG_PRINTF("\nGateway address     ");
  uip_debug_ipaddr_print(&router_cfg.gw_addr);
  LOG_PRINTF("\nUnsolicited address ");
  uip_debug_ipaddr_print(&router_cfg.unsolicited_dest);
  LOG_PRINTF("\n");

  if (!sl_zip_router_reset()) {
    ERR_PRINTF("Fatal error\n");
  }

  sl_class_zip_node_init();
  zgw_component_start(ZGW_BRIDGE);
  bridge_init();
  LOG_PRINTF("Coming up\n");
  sl_ts_init();
  rd_init(false);
}
