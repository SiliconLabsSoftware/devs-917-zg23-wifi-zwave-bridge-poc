/***************************************************************************/ /**
 * @file sl_security_layer.c
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

#include "Common/sl_rd_types.h"
#include "Common/sl_common_log.h"
#include "Common/sl_common_config.h"
#include "Common/sl_gw_info.h"
#include "utls/sl_node_sec_flags.h"

#include "Serialapi.h"
#include "sl_sleeptimer.h"

#include "transport/sc_types.h"
#include "transport/Secure_learnRequired.h"
#include "transport/Secure_learn.h"
#include "transport/sl_ts_param.h"
#include "transport/sl_ts_common.h"
#include "transport/sl_ts_s0.h"
#include "transport/sl_security_scheme0.h"
#include "transport/sl_zw_send_data.h"
#include "transport/sl_zw_send_request.h"
#include "transport/ZW_PRNG.h"

#include "ip_translate/sl_zw_resource.h"

#include "sl_security_layer.h"

#define SECURITY_SCHEME_0_BIT 0x1

//Number of implemented schemes
#define N_SCHEMES 1

static Secure_learn ctx;
static sl_sleeptimer_timer_handle_t timer;
static sec_learn_complete_t callback;
security_scheme_t net_scheme = NO_SCHEME;

static ZW_NETWORK_KEY_VERIFY_FRAME key_verify = { COMMAND_CLASS_SECURITY, NETWORK_KEY_VERIFY };

static u8_t *secureCommandClassesList[N_SCHEMES] = { 0 }; /* List of supported command classes, when node communicate by this transport */
static u8_t secureCommandClassesListCount[N_SCHEMES]; /* Count of elements in supported command classes list */

/*Forward */
static void send_and_raise(nodeid_t node, BYTE *pBufData, BYTE dataLength, BYTE txOptions, security_scheme_t scheme);

static ts_param_t cur_tsparm;

static void send_and_raise_ex(BYTE *pBufData, BYTE dataLength);
void sl_application_cmd_zip_handler(ts_param_t *p,
                                    ZW_APPLICATION_TX_BUFFER *pCmd,
                                    uint16_t cmdLength);

/**
 * These frames are never used at the same time.
 */
static union {
  ZW_SECURITY_SCHEME_INHERIT_FRAME scheme_inherit_frame;
  ZW_SECURITY_SCHEME_REPORT_FRAME scheme_report_frame;
  ZW_SECURITY_SCHEME_GET_FRAME scheme_get_frame;
  u8_t key_set_frame[16 + 2];
  u8_t cmd[64];
} tx;

/************************************* Common functions *******************************************/
/**
 * Read NVM of Z-Wave module
 */
int
zw_appl_nvm_read(uint16_t start, void* dst, uint8_t size)
{
  return MemoryGetBuffer(start, dst, size);
}

/**
 * Write NVM of Z-Wave module
 */
void
zw_appl_nvm_write(uint16_t start, void* dst, uint8_t size)
{
  MemoryPutBuffer(start, (BYTE*)dst, size, 0);
}

#define nvm_config_get(a, b)  zw_appl_nvm_read(a, b, 16)
#define nvm_config_set(a, b)  zw_appl_nvm_write(a, b, 16)
#define MAGIC_KEY     "670E195A67502A77864512D113DB50D3"

const uint8_t CONST_MAGIC_KEY[] = {
  0x43, 0x4E, 0x26, 0xDF, 0xC0, 0x04, 0x78, 0xCB, 0x64, 0xFC, 0x61, 0x74, 0x5B, 0xDC, 0xFF, 0xED
};

uint8_t assigned_keys = KEY_CLASS_S0;

bool keystore_network_key_read(uint8_t keyclass, uint8_t *buf)
{
  if (0 == (keyclass & assigned_keys)) {
    return 0;
  }

  if (keyclass == KEY_CLASS_S0) {
    memcpy(buf, CONST_MAGIC_KEY, sizeof(CONST_MAGIC_KEY));
  } else {
    assert(0);
    return 0;
  }

  DBG_PRINTF("Key class 0x%02x: \n", keyclass);
  sl_print_key(buf);

  return 1;
}

bool keystore_network_key_write(uint8_t keyclass, uint8_t *buf)
{
  if (keyclass == KEY_CLASS_S0) {
    sec0_set_key(buf);
  } else {
    assert(0);
    return 0;
  }

  return 1;
}

void keystore_network_generate_key_if_missing()
{
  uint8_t key_store_flags;
  key_store_flags = assigned_keys;

  if ((key_store_flags & KEY_CLASS_S0) == 0) {
    DBG_PRINTF("KEY_CLASS_S0 was missing. Generating new.\n");
    memcpy(networkKey, CONST_MAGIC_KEY, 16);
    sl_print_hex_to_string(networkKey, 16);
    keystore_network_key_write(KEY_CLASS_S0, networkKey);
  }
}

uint8_t security_scheme = KEY_CLASS_S0;
/*
 * Load security states from eeprom
 */
void security_load_state()
{
  int8_t scheme = AUTO_SCHEME;
  keystore_network_key_read(KEY_CLASS_S0, networkKey);
  secure_learnIface_set_net_scheme(&ctx, scheme);
  sec0_set_key(networkKey);
}

/*
 * Save security states to eeprom
 */
void security_save_state()
{
  int8_t scheme;
  scheme = (int8_t)secure_learnIface_get_net_scheme(&ctx);
  DBG_PRINTF("Setting scheme %x\n", scheme);
  keystore_network_key_write(KEY_CLASS_S0, networkKey);
}

void security_init()
{
  static int prng_initialized = 0;
  if (!prng_initialized) {
    // DS
    #if S2_SUPPORTED
    S2_init_prng();
    #endif
    InitPRNG();
    prng_initialized = 1;
  }

  secure_learn_init(&ctx);
  secure_learn_enter(&ctx);

  /* Initialize the transport layer*/
  if (ZW_GetSUCNodeID() == MyNodeID) {
    keystore_network_generate_key_if_missing();
  }

  sec0_init();

  security_load_state();
// DS
#if S2_SUPPORTED
  sec2_init();
#endif
}

void security_set_supported_classes(u8_t* classes, u8_t n_classes)
{
  secureCommandClassesList[0] = classes;
  secureCommandClassesListCount[0] = n_classes;
}

void security_set_default()
{
  u8_t scheme = 0;

  if (ZW_Type_Library() & (ZW_LIB_CONTROLLER_STATIC | ZW_LIB_CONTROLLER | ZW_LIB_CONTROLLER_BRIDGE)) {
    scheme = secure_learnIface_get_supported_schemes(&ctx);
    sec0_reset_netkey();
  }
  secure_learnIface_set_net_scheme(&ctx, scheme);
  security_save_state();
  security_init();
}

static void send_data_callback(BYTE status, void*user, TX_STATUS_TYPE *t)
{
  (void) t;
  (void) user;

  if (status == TRANSMIT_COMPLETE_OK) {
    DBG_PRINTF("TX done ok\n");
    secure_learnIface_raise_tx_done(&ctx);
  } else {
    DBG_PRINTF("TX done fail\n");
    secure_learnIface_raise_tx_fail(&ctx);
  }
}

/**
 * timeout function used by the statemachine timer
 */
static void timeout(void* user)
{
  secure_learn_raiseTimeEvent(&ctx, user);
}

void sl_timer_timeout(sl_sleeptimer_timer_handle_t *t, void *u)
{
  (void) t;
  timeout(u);
}

void secure_learn_setTimer(Secure_learn* handle, const sc_eventid evid, const sc_integer time_ms, const sc_boolean periodic)
{
  (void) handle;
  (void) periodic;
  sl_sleeptimer_start_timer_ms(&timer, time_ms, sl_timer_timeout, evid, 1, 0);
}

void secure_learn_unsetTimer(Secure_learn* handle, const sc_eventid evid)
{
  (void) handle;
  (void) evid;
  sl_sleeptimer_stop_timer(&timer);
}

void secure_poll()
{
  int s = -1;

  while (s != ctx.stateConfVector[0]) {
    s = ctx.stateConfVector[0];

    secure_learn_runCycle(&ctx);
  }
}

void secure_learnIface_send_commands_supported(const sc_integer node, const sc_integer snode, const sc_integer txOptions)
{
  (void) node;
  (void) txOptions;
  (void) snode;
  u8_t scheme = 0;
  int len;

  tx.cmd[0] = COMMAND_CLASS_SECURITY;
  tx.cmd[1] = SECURITY_COMMANDS_SUPPORTED_REPORT;
  tx.cmd[2] = 0;

  DBG_PRINTF("Sending commands supported report net_scheme is %s\n", network_scheme_name(net_scheme));

  if (
    (cur_tsparm.snode == MyNodeID) && (net_scheme == SECURITY_SCHEME_0)
    ) {
    memcpy(&tx.cmd[3], secureCommandClassesList[scheme], secureCommandClassesListCount[0]);
    len = secureCommandClassesListCount[scheme];
// DS
#if (UNSOLICT_SUPPORTED)
    /* Add Unsolicited Destination CCs */
    roomLeft = sizeof(tx.cmd) - 3 - len;
    copyLen = nSecureClassesPAN < roomLeft ? nSecureClassesPAN : roomLeft;
    memcpy(&tx.cmd[3 + secureCommandClassesListCount[scheme]], SecureClassesPAN, copyLen);
    len += copyLen;
#endif
  } else {
    secureCommandClassesListCount[scheme] = 0;
    len = 0;
  }
  send_and_raise_ex( (BYTE *) tx.cmd, 3 + len);
}

static void handle_network_key_set(ts_param_t* p, const ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength, BYTE txOption)
{
  if (p->scheme == SECURITY_SCHEME_0
      && secure_learn_isActive(&ctx, Secure_learn_main_region_LearnMode_r1_Scheme_report)
      && cmdLength >= (sizeof(ZW_NETWORK_KEY_SET_1BYTE_FRAME) + 15)
      ) {
    // ZW_NetworkKeySet1byteFrame is 1 byte + 15 bytes of key following Z-Wave protocol
    memcpy(networkKey, &pCmd->ZW_NetworkKeySet1byteFrame.networkKeyByte1, 16); //NOSONAR
    sec0_set_key(networkKey);
    secure_learnIface_set_txOptions(&ctx, txOption);
    secure_learnIface_raise_key_set(&ctx, p->snode);
  }
}

static void handle_security_scheme_get(ts_param_t* p, const ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength, BYTE txOption)
{
  MyNodeID = p->dnode;
  if (p->scheme == NO_SCHEME && cmdLength >= sizeof(ZW_SECURITY_SCHEME_GET_FRAME) ) {
    secure_learnIface_set_scheme(&ctx, (pCmd->ZW_SecuritySchemeGetFrame.supportedSecuritySchemes & 0xFE) | 0x1);
    secure_learnIface_set_txOptions(&ctx, txOption);
    secure_learnIface_raise_scheme_get(&ctx, p->snode);
  }
}

static void handle_security_scheme_inherit(ts_param_t* p, const ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength, BYTE txOption)
{
  if (p->scheme == SECURITY_SCHEME_0 && cmdLength >= sizeof(ZW_SECURITY_SCHEME_INHERIT_FRAME) ) {
    if ((pCmd->ZW_SecuritySchemeInheritFrame.supportedSecuritySchemes & SECURITY_SCHEME_0_BIT) == 0) {
      secure_learnIface_set_txOptions(&ctx, txOption);
      secure_learnIface_raise_scheme_inherit(&ctx, p->snode);
    }
  }
}

static void handle_security_scheme_report(ts_param_t* p, const ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength)
{
  if (p->scheme == SECURITY_SCHEME_0 || secure_learn_isActive(&ctx, Secure_learn_main_region_InclusionMode_r1_SchemeRequest)) {
    if (cmdLength >= sizeof(ZW_SECURITY_SCHEME_REPORT_FRAME) ) {
      secure_learnIface_set_scheme(&ctx, (pCmd->ZW_SecuritySchemeReportFrame.supportedSecuritySchemes & 0xFE) | 0x1);
      secure_learnIface_raise_scheme_report(&ctx, p->snode);
    }
  }
}

static void handle_network_key_verify(ts_param_t* p)
{
  if (p->scheme == SECURITY_SCHEME_0) {
    secure_learnIface_raise_key_verify(&ctx, p->snode);
  }
}

static void handle_security_commands_supported_get(ts_param_t* p)
{
  if (p->scheme == SECURITY_SCHEME_0) {
    ts_param_make_reply(&cur_tsparm, p);
    secure_learnIface_raise_commandsSupportedRequest(&ctx);
  }
}

static void handle_security_nonce_get(ts_param_t* p)
{
  if (p->scheme == NO_SCHEME) {
    sec0_send_nonce(p);
  }
}

static void handle_security_nonce_report(ts_param_t* p, const ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength)
{
  if (p->scheme == NO_SCHEME && cmdLength >= sizeof(ZW_SECURITY_NONCE_REPORT_FRAME)) {
    sec0_register_nonce(p->snode, p->dnode, &pCmd->ZW_SecurityNonceReportFrame.nonceByte1);
  }
}

static void handle_security_message_encapsulation(ts_param_t* p, const ZW_APPLICATION_TX_BUFFER *pCmd, BYTE cmdLength)
{
  uint8_t rxBuffer[128];
  uint8_t len;
  LOG_PRINTF("SECURITY_MESSAGE_ENCAPSULATION\n");
  if (secure_learn_isActive(&ctx, Secure_learn_main_region_Idle)
      && (isNodeBad(p->dnode) || isNodeBad(p->snode)) ) {
    WRN_PRINTF("Dropping security package from KNOWN BAD NODE\n");
    return;
  }

  len = sec0_decrypt_message(p->snode, p->dnode, (const uint8_t*)pCmd, cmdLength, rxBuffer);
  if (len) {
    if (!isNodeSecure(p->snode)) {
      secure_learnIfaceI_register_scheme(p->snode, 1 << SECURITY_SCHEME_0);
    }
    p->scheme = SECURITY_SCHEME_0;
    sl_application_cmd_zip_handler(p, (ZW_APPLICATION_TX_BUFFER*)rxBuffer, len);
  }
  if (pCmd->ZW_Common.cmd == SECURITY_MESSAGE_ENCAPSULATION_NONCE_GET) {
    sec0_send_nonce(p);
  }
}

void /*RET Nothing                  */
security_CommandHandler(ts_param_t* p,
                        const ZW_APPLICATION_TX_BUFFER *pCmd, /* IN Payload from the received frame, the union */
/*    should be used to access the fields */ BYTE cmdLength) /* IN Number of command bytes including the command */
{
  LOG_PRINTF("security_CommandHandler: %d\n", pCmd->ZW_Common.cmd);
  BYTE txOption;

  txOption = ((p->rx_flags & RECEIVE_STATUS_LOW_POWER) ? TRANSMIT_OPTION_LOW_POWER : 0) | TRANSMIT_OPTION_ACK
             | TRANSMIT_OPTION_EXPLORE;

  if (pCmd->ZW_Common.cmdClass != COMMAND_CLASS_SECURITY && p->dnode != MyNodeID) {
    return;
  }

  if (secure_learn_isActive(&ctx, Secure_learn_main_region_Idle) && (assigned_keys & KEY_CLASS_S0) <= 0) {
    return;
  }

  switch (pCmd->ZW_Common.cmd) {
    case NETWORK_KEY_SET:
      handle_network_key_set(p, pCmd, cmdLength, txOption);
      break;
    case SECURITY_SCHEME_GET:
      handle_security_scheme_get(p, pCmd, cmdLength, txOption);
      break;
    case SECURITY_SCHEME_INHERIT:
      handle_security_scheme_inherit(p, pCmd, cmdLength, txOption);
      break;
    case SECURITY_SCHEME_REPORT:
      handle_security_scheme_report(p, pCmd, cmdLength);
      break;
    case NETWORK_KEY_VERIFY:
      handle_network_key_verify(p);
      break;
    case SECURITY_COMMANDS_SUPPORTED_GET:
      handle_security_commands_supported_get(p);
      break;
    case SECURITY_NONCE_GET:
      handle_security_nonce_get(p);
      break;
    case SECURITY_NONCE_REPORT:
      handle_security_nonce_report(p, pCmd, cmdLength);
      break;
    case SECURITY_MESSAGE_ENCAPSULATION_NONCE_GET:
    case SECURITY_MESSAGE_ENCAPSULATION:
      handle_security_message_encapsulation(p, pCmd, cmdLength);
      break;
  }
}

/**
 * Store security state, ie. save scheme and keys.
 */
void secure_learnIfaceL_save_state()
{
  security_save_state();
}
/**
 * New keys should be generated.
 */
void secure_learnIfaceL_new_keys()
{
  sec0_reset_netkey();
}

void secure_learnIface_complete(const sc_integer scheme_mask)
{
  uint8_t flags = 0;
  /*Update eeprom with negotiated scheme*/
  if (scheme_mask > 0) {
    LOG_PRINTF("Secure add/inclusion succeeded, with schemes 0x%lx\n", scheme_mask);
    flags = NODE_FLAG_SECURITY0;
  } else if (scheme_mask < 0) {
    flags = NODE_FLAG_KNOWN_BAD;
    ERR_PRINTF("Secure add/inclusion failed\n");
    sec0_set_key(networkKey); // Reinstate the real network key now that bootstrapping has failed
  }

  if (callback) {
    callback(flags);
  }
}

static void send_and_raise_ex(BYTE *pBufData, BYTE dataLength)
{
  if (!sl_zw_send_data_appl(&cur_tsparm, pBufData, dataLength, send_data_callback, 0)) {
    secure_learnIface_raise_tx_fail(&ctx);
  }
}
/**
 * SendData wrapper that raises a tx_done or tx_fial on completion.
 * Send using highest available security scheme
 */
static void send_and_raise(nodeid_t node, BYTE *pBufData, BYTE dataLength, BYTE txOptions, security_scheme_t scheme)
{
  ts_param_t p;
  ts_set_std(&p, node);
  p.scheme = scheme;
  p.tx_flags = txOptions;

  if (!sl_zw_send_data_appl(&p, pBufData, dataLength, send_data_callback, 0)) {
    secure_learnIface_raise_tx_fail(&ctx);
  }
}

int8_t is_sec0_key_granted()
{
  return NODE_FLAG_SECURITY0;
}

/****************************** Learn mode related functions *************************************/

/**
 * Enter learn mode
 */
void security_learn_begin(sec_learn_complete_t __cb)
{
  if (secure_learn_isActive(&ctx, Secure_learn_main_region_Idle)) { // Annoying check but needed to protect the isController variable
    callback = __cb;

    if (ZW_Type_Library() & (ZW_LIB_CONTROLLER_STATIC | ZW_LIB_CONTROLLER | ZW_LIB_CONTROLLER_BRIDGE)) {
      secure_learnIface_set_isController(&ctx, TRUE);
    } else {
      secure_learnIface_set_isController(&ctx, FALSE);
    }
    LOG_PRINTF("security_learn_begin\n");
    secure_learnIface_raise_learnRequest(&ctx);
  }
}

void secure_learnIfaceL_send_scheme_report(const sc_integer node, const sc_integer txOptions)
{
  u8_t ctrlScheme; // The including controllers scheme
  u8_t my_schemes;
  ctrlScheme = secure_learnIface_get_scheme(&ctx);
  my_schemes = secure_learnIface_get_supported_schemes(&ctx) & ctrlScheme; //Common schemes

  tx.scheme_report_frame.cmdClass = COMMAND_CLASS_SECURITY;
  tx.scheme_report_frame.cmd = SECURITY_SCHEME_REPORT;
  tx.scheme_report_frame.supportedSecuritySchemes = my_schemes;

  /* secure_learnIface_get_net_scheme () function is only used here during S0 bootstrapping, anywhere else we should use
   * is_sec0_key_granted () function that reads the S0 key from keystore.
   * During S0 bootstrapping we cannot trust the is_sec0_key_granted() function because the S0 key has not yet been written to the keystore.
   */
  if (secure_learnIface_get_net_scheme(&ctx) == 0) {
    send_and_raise(node, (BYTE *) &tx.scheme_report_frame, sizeof(ZW_SECURITY_SCHEME_REPORT_FRAME), txOptions, NO_SCHEME);
  } else {
    send_and_raise(node, (BYTE *) &tx.scheme_report_frame, sizeof(ZW_SECURITY_SCHEME_REPORT_FRAME), txOptions, SECURITY_SCHEME_0);
  }
}

void secure_learnIfaceL_set_inclusion_key()
{
  BYTE key[16];
  memset(key, 0, sizeof(key));
  sec0_set_key(key);
}

void secure_learnIfaceL_send_key_verify(const sc_integer node, const sc_integer txOptions)
{
  send_and_raise(node, (BYTE *) &key_verify, sizeof(key_verify), txOptions, SECURITY_SCHEME_0);
}

/********************************** Add node related ******************************************/

u8_t security_add_begin(u8_t node, u8_t txOptions, BOOL controller, sec_learn_complete_t __cb)
{
  if (secure_learn_isActive(&ctx, Secure_learn_main_region_Idle)  && !isNodeSecure(node)) { // Annoying check but needed to protect the isController variable
    LOG_PRINTF("Secure add begin\n");
    callback = __cb;
    secure_learnIface_set_isController(&ctx, controller);
    secure_learnIface_set_txOptions(&ctx, txOptions);
    secure_learnIface_raise_inclusionRequest(&ctx, node);
    return TRUE;
  } else {
    return FALSE;
  }
}

void secure_learnIfaceI_send_scheme_get(const sc_integer node, const sc_integer txOptions)
{
  tx.scheme_get_frame.cmdClass = COMMAND_CLASS_SECURITY;
  tx.scheme_get_frame.cmd = SECURITY_SCHEME_GET;
  tx.scheme_get_frame.supportedSecuritySchemes = 0;
  send_and_raise(node, (BYTE *) &tx.scheme_get_frame, sizeof(tx.scheme_get_frame), txOptions, NO_SCHEME);
}

void secure_learnIfaceI_send_key(const sc_integer node, const sc_integer txOptions)
{
  u8_t inclusionKey[16];
  tx.key_set_frame[0] = COMMAND_CLASS_SECURITY;
  tx.key_set_frame[1] = NETWORK_KEY_SET;
  memcpy(tx.key_set_frame + 2, networkKey, 16);

  /*It might be better to do this in some other way, maybe a txOption .... */
  memset(inclusionKey, 0, 16);
  sec0_set_key(inclusionKey);

  send_and_raise(node, (BYTE *) &tx.key_set_frame, sizeof(tx.key_set_frame), txOptions, SECURITY_SCHEME_0);
}

void secure_learnIfaceI_send_scheme_inherit(const sc_integer node, const sc_integer txOptions)
{
  tx.scheme_inherit_frame.cmdClass = COMMAND_CLASS_SECURITY;
  tx.scheme_inherit_frame.cmd = SECURITY_SCHEME_INHERIT;
  tx.scheme_inherit_frame.supportedSecuritySchemes = 0;
  send_and_raise(node, (BYTE *) &tx.scheme_inherit_frame, sizeof(tx.scheme_inherit_frame), txOptions, SECURITY_SCHEME_0);
}

void secure_learnIfaceI_restore_key()
{
  sec0_set_key(networkKey);
}

/**
 * Register which schemes are supported by a node
 */
void secure_learnIfaceI_register_scheme(const sc_integer node, const sc_integer scheme)
{
  if (scheme & SECURITY_SCHEME_0_BIT) {
    LOG_PRINTF("Registering node %ld with schemes %d\n", node, SECURITY_SCHEME_0_BIT);
    sl_set_cache_entry_flag_masked(node, NODE_FLAG_SECURITY0, NODE_FLAG_SECURITY0);
  } else {
    sl_set_cache_entry_flag_masked(node, 0, NODE_FLAG_SECURITY0);
  }
}

uint8_t secure_learn_active()
{
  return !secure_learn_isActive(&ctx, Secure_learn_main_region_Idle);
}

void sec0_abort_inclusion()
{
  if (!secure_learn_isActive(&ctx, Secure_learn_main_region_Idle)) {
    DBG_PRINTF("S0 inclusion was aborted");
    secure_learn_init(&ctx);
    secure_learn_enter(&ctx);
    sl_sleeptimer_stop_timer(&timer);
  }
}
