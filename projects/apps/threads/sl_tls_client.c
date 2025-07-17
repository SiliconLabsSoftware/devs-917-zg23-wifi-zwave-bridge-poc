/*******************************************************************************
 * @file  sl_tls_client.c
 * @brief TLS client interface for Z-Wave applications
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

#include "lwip/sockets.h"
#include "mbedtls/ssl.h"
#include "mbedtls/net_sockets.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/x509_crt.h"
#include <string.h>
#include <stdio.h>

#include "cacert.pem.h"
#include "servercert.pem.h"
#include "serverkey.pem.h"

#include "sl_common_log.h"
#include "ZW_udp_server.h"
#include "Net/sl_udp_utils.h"
#include "sl_net.h"
#include "sl_tcpip_handler.h"

#include "sl_ota/sl_ota.h"

#define SL_IP_NAME_LENGTH_BYTES 32
#define PORT_NAME   "8000"
#define BUFFER_SIZE 1470
unsigned char gg_buf[BUFFER_SIZE];

mbedtls_net_context server_fd;
uint32_t flags;
int sl_tls_enabled = 0;
uint8_t sl_serv_ip[SL_IP_NAME_LENGTH_BYTES];
uint16_t sl_serv_port = 8000;

static mbedtls_ssl_context ssl;
static mbedtls_ssl_config conf;
static mbedtls_entropy_context entropy;
static mbedtls_ctr_drbg_context ctr_drbg;
static mbedtls_x509_crt x_cacert;

static const char *pers = "tls_client";

const osThreadAttr_t sl_tcp_thread_attr = {
  .name       = "tls_t",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 10240,
  .priority   = osPriorityNormal6,
  .tz_module  = 0,
  .reserved   = 0,
};

/*==============================================*/
/**
 * @fn         tls_client_run
 * @brief
 * @return     none.
 * @section description handler connect and data from tls server.
 */
static int tls_client_init(const char *serv_ip, const char *serv_port);
static int tls_client_handshake(void);
static void tls_client_verify_cert(void);
static void tls_client_handle_data(void);
static void tls_client_cleanup(void);

void tls_client_run(const char *serv_ip, const char *serv_port)
{
  int ret = tls_client_init(serv_ip, serv_port);
  if (ret != 0) {
    goto exit;
  }

  ret = tls_client_handshake();
  if (ret != 0) {
    goto exit;
  }

  tls_client_verify_cert();
  tls_client_handle_data();

  exit:
  LOG_PRINTF("exit tls\n");
  tls_client_cleanup();
}

static int tls_client_init(const char *serv_ip, const char *serv_port)
{
  int ret;
  mbedtls_net_init(&server_fd);
  mbedtls_ssl_init(&ssl);
  mbedtls_ssl_config_init(&conf);
  mbedtls_x509_crt_init(&x_cacert);
  mbedtls_ctr_drbg_init(&ctr_drbg);

  LOG_PRINTF("INIT DONE!\n");

  mbedtls_entropy_init(&entropy);
  if ((ret = mbedtls_ctr_drbg_seed(&ctr_drbg,
                                   mbedtls_entropy_func,
                                   &entropy,
                                   (const unsigned char *) pers,
                                   strlen(pers))) != 0) {
    LOG_PRINTF("mbedtls_ctr_drbg_seed returned %d\n", ret);
    return ret;
  }

  ret = mbedtls_x509_crt_parse(&x_cacert,
                               (const unsigned char *) cacert,
                               sizeof(cacert));
  if (ret < 0) {
    LOG_PRINTF("mbedtls_x509_crt_parse returned -0x%x\n",
               (unsigned int) -ret);
    return ret;
  }

  if ((ret = mbedtls_ssl_config_defaults(&conf,
                                         MBEDTLS_SSL_IS_CLIENT,
                                         MBEDTLS_SSL_TRANSPORT_STREAM,
                                         MBEDTLS_SSL_PRESET_DEFAULT)) != 0) {
    LOG_PRINTF(" failed\n  ! mbedtls_ssl_config_defaults returned %d\n", ret);
    return ret;
  }
  mbedtls_ssl_conf_min_version(&conf,
                               MBEDTLS_SSL_MAJOR_VERSION_3,
                               MBEDTLS_SSL_MINOR_VERSION_3);
  mbedtls_ssl_conf_max_version(&conf,
                               MBEDTLS_SSL_MAJOR_VERSION_3,
                               MBEDTLS_SSL_MINOR_VERSION_3);

  mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
  mbedtls_ssl_conf_ca_chain(&conf, &x_cacert, NULL);
  mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);

  if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0) {
    LOG_PRINTF("mbedtls_ssl_setup returned %d\n", ret);
    return ret;
  }

  mbedtls_ssl_set_hostname(&ssl, "example.com");

  mbedtls_ssl_set_bio(&ssl,
                      &server_fd,
                      mbedtls_net_send,
                      mbedtls_net_recv,
                      NULL);

  if ((ret = mbedtls_net_connect(&server_fd,
                                 serv_ip,
                                 serv_port,
                                 MBEDTLS_NET_PROTO_TCP)) != 0) {
    LOG_PRINTF("error connect\n");
    return ret;
  }
  return 0;
}

static int tls_client_handshake(void)
{
  int ret;
  LOG_PRINTF("Performing the SSL/TLS handshake...\n");

  while ((ret = mbedtls_ssl_handshake(&ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      LOG_PRINTF("mbedtls_ssl_handshake returned -0x%x\n",
                 (unsigned int) -ret);
      return ret;
    }
    osDelay(100);
  }
  return 0;
}

static void tls_client_verify_cert(void)
{
  LOG_PRINTF("Verifying peer X.509 certificate...\n");

  if ((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0) {
    char vrfy_buf[512];
    LOG_PRINTF("X.509 certificate failed\n");
    mbedtls_x509_crt_verify_info(vrfy_buf, sizeof(vrfy_buf), "  ! ", flags);
    LOG_PRINTF("%s\n", vrfy_buf);
  }
}

static void tls_client_handle_node_ctrl(sl_srv_data_t *pkt, int len)
{
  LOG_PRINTF("Forwarding to Z-Wave port\n");
  struct sockaddr_in ipaddr;
  sl_ipv4_address_t ip = { 0 };
  sl_net_inet_addr((const char *) sl_serv_ip, (uint32_t *) &ip);
  ipaddr.sin_port        = ZWAVE_PORT;
  ipaddr.sin_family      = AF_INET;
  ipaddr.sin_addr.s_addr = ip.value;
  sl_tcpip_buf_t *zippkt = sl_zip_packet_v4(&ipaddr, pkt->data, (uint16_t)(len - 1));
  if (zippkt) {
    zw_tcpip_post_event(1, zippkt);
  }
}

static int tls_client_send_ack(uint32_t rlen)
{
  int ret;
  do {
    osDelay(10);
    ret = mbedtls_ssl_write(&ssl, gg_buf, rlen);
    if (ret == MBEDTLS_ERR_SSL_WANT_READ
        || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    } else if (ret < 0) {
      LOG_PRINTF("failed\n  ! mbedtls_ssl_write returned %d\n\n", ret);
      return ret;
    }
  } while (ret <= 0);
  return 0;
}

static void tls_client_handle_ota_bridge(sl_srv_data_t *pkt, int len)
{
  uint32_t rlen = 0;
  LOG_PRINTF("Forwarding to OTA bridge\n");
  sl_ota_bridge_handler(pkt->data, (uint16_t)(len - 1));
  if ((len - 1) > 4) {
    sl_ota_bridge_ack(gg_buf, &rlen);
    tls_client_send_ack(rlen);
  }
}

static void tls_client_handle_ota_controller(sl_srv_data_t *pkt, int len)
{
  uint32_t rlen = 0;
  LOG_PRINTF("Forwarding to OTA controller\n");
  sl_ota_download_controller_fw(pkt->data, (uint16_t)(len - 1));
  if ((len - 1) > 4) {
    sl_ota_bridge_ack(gg_buf, &rlen);
    tls_client_send_ack(rlen);
  }
}

static int sli_tls_node_download(sl_srv_data_t *pkt, uint32_t len)
{
  uint32_t rlen = 0;
  int ret = 0;
  LOG_PRINTF("Forwarding to OTA Node\n");
  ret = sl_ota_download_node_fw(pkt->data, len - 1);
  if ((len - 1) > 4) {
    sl_ota_bridge_ack(gg_buf, &rlen);
    tls_client_send_ack(rlen);
  }
  return ret;
}

static void tls_client_handle_data(void)
{
  int ret;
  int len;
  sl_srv_data_t *pkt;
  LOG_PRINTF("  < Read from server:");
  int exit_loop = 0;
  while (!exit_loop) {
    len = sizeof(gg_buf) - 1;
    memset(gg_buf, 0, sizeof(gg_buf));
    ret = mbedtls_ssl_read(&ssl, gg_buf, len);

    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
      continue;
    }

    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
      exit_loop = 1;
      continue;
    }

    if (ret < 0) {
      LOG_PRINTF("mbedtls_ssl_read returned %d\n\n", ret);
      exit_loop = 1;
      continue;
    }

    if (ret == 0) {
      LOG_PRINTF("\n\nEOF\n\n");
      exit_loop = 1;
      continue;
    }

    len = ret;
    LOG_PRINTF("tcp data: %d\n", len);
    pkt = (sl_srv_data_t *) gg_buf;
    switch (pkt->cmd) {
      case SL_SRV_NODE_CTRL_COMMAND:
        tls_client_handle_node_ctrl(pkt, len);
        break;
      case SL_SRV_OTA_BRIDGE_COMMAND:
        tls_client_handle_ota_bridge(pkt, len);
        break;
      case SL_SRV_OTA_CONTROLLER_COMMAND:
        tls_client_handle_ota_controller(pkt, len);
        break;
      case SL_SRV_OTA_NODE_COMMAND:
        sli_tls_node_download(pkt, len);
        break;
      default:
        LOG_PRINTF("Unknown command: %d\n", pkt->cmd);
        break;
    }
    osDelay(10);
  }

  do {
    mbedtls_ssl_close_notify(&ssl);
  } while (ret == MBEDTLS_ERR_SSL_WANT_WRITE);

  ret = mbedtls_ssl_session_reset(&ssl);
  if (ret != 0) {
    LOG_PRINTF("mbedtls_ssl_session_reset returned %d\n", ret);
  }
}

static void tls_client_cleanup(void)
{
  mbedtls_net_free(&server_fd);
  mbedtls_x509_crt_free(&x_cacert);
  mbedtls_ssl_free(&ssl);
  mbedtls_ssl_config_free(&conf);
  mbedtls_ctr_drbg_free(&ctr_drbg);
  mbedtls_entropy_free(&entropy);
}

/*==============================================*/
/**
 * @fn         sl_tcp_client_enable
 * @brief
 * @return     none.
 * @section description update IP done and start run tls client mode.
 */
void sl_tcp_client_enable(int en)
{
  sl_tls_enabled = en;
}

/*==============================================*/
/**
 * @fn         sl_tcp_client_set_ip_start
 * @brief
 * @return     none.
 * @section description update IP server from CLI command
 */
void sl_tcp_client_set_ip_start(uint8_t *ip, uint16_t port)
{
  if (strlen((const char*)ip) >= sizeof(sl_serv_ip)) {
    return;
  }
  strcpy((char *) sl_serv_ip, (const char *) ip);
  if (port) {
    sl_serv_port = port;
  }
}

/*==============================================*/
/**
 * @fn         sl_tcp_client_thread
 * @brief
 * @return     none.
 * @section description task handler connect and data from tls server.
 */
void sl_tcp_client_thread(void *argv)
{
  (void) argv;
  while (1) {
    if (sl_tls_enabled != 1) {
      osDelay(1000);
      continue;
    }
    char port_name[10];
    snprintf(port_name, sizeof(port_name), "%d", sl_serv_port);
    LOG_PRINTF("tls: %s, %s\n", sl_serv_ip, port_name);
    tls_client_run((const char *) sl_serv_ip, port_name);
    osDelay(10000);
  }
}

/*==============================================*/
/**
 * @fn         sl_tls_client_init
 * @brief
 * @return     none.
 * @section description init thread tls client.
 */
void sl_tls_client_init(void)
{
  LOG_PRINTF("tcp tls client thread start\n");
  if (!osThreadNew((osThreadFunc_t) sl_tcp_client_thread,
                   NULL,
                   &sl_tcp_thread_attr)) {
    LOG_PRINTF("tcp tls thread FAIL!\n");
  }
}
