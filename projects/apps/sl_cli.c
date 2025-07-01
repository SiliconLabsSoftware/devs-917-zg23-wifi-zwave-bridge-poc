/*******************************************************************************
 * @file  sl_cli.c
 * @brief CLI functions
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
/*************************************************************************
 *
 */

#include "sl_ota/sl_bridge_ota.h"
#include "sl_common_log.h"
#include "sl_cli.h"
#include "console.h"
#include "sl_constants.h"
#include "sl_status.h"
#include "sl_board_configuration.h"
#include "cmsis_os2.h"
#include "sl_utility.h"
#include <stdbool.h>
#include <string.h>
#include "lwip/priv/nd6_priv.h"
#include <lwip/nd6.h>
#include <lwip/ip6_addr.h>
#include <lwip/netif.h>

/****************************************************************************/
/*                            LOCAL VARIABLES                               */
/****************************************************************************/
const osThreadAttr_t sli_cli_thread_attributes = {
  .name       = "cli_thread",
  .attr_bits  = 0,
  .cb_mem     = 0,
  .cb_size    = 0,
  .stack_mem  = 0,
  .stack_size = 1024,
  .priority   = osPriorityNormal,
  .tz_module  = 0,
  .reserved   = 0,
};

static void
sli_print_command_args(const console_descriptive_command_t *command);
void sli_print_status(sl_status_t status, uint32_t duration);

// Argument list
const char *ota_options_type[] = { "bridge",
                                   "controller",
                                   "end-device",
                                   "stop",
                                   NULL };

const arg_list_t console_argument_types[] = {
  [CONSOLE_TYPE(ota_options)] = ota_options_type,
};
const value_list_t console_argument_values[] = {
  [CONSOLE_TYPE(ota_options)] = (const uint32_t[]){ OTA_BRIDGE,
                                                    OTA_CONTROLLER,
                                                    OTA_END_DEVICE,
                                                    OTA_STOP },
};

// Command list
sl_status_t sli_help_command_handler(console_args_t *arguments);
static const char *sli_help_arg_help[]                      = {};
static const console_descriptive_command_t sli_help_command = {
  .description   = "Print help",
  .argument_help = sli_help_arg_help,
  .handler       = sli_help_command_handler,
  .argument_list = { CONSOLE_ARG_END }
};

sl_status_t sli_dummy_handler(console_args_t *arguments);
static const char *sli_dummy_arg_help[] = { "setkey GW_KEY" };
static const console_descriptive_command_t sli_dummy_command = {
  .description   = "Dummy command",
  .argument_help = sli_dummy_arg_help,
  .handler       = sli_dummy_handler,
  .argument_list = { CONSOLE_ARG_END }
};

sl_status_t sli_ota_handler(console_args_t *arguments);
static const char *sli_ota_arg_help[]                      = { 0 };
static const console_descriptive_command_t sli_ota_command = {
  .description   = "OTA command",
  .argument_help = sli_ota_arg_help,
  .handler       = sli_ota_handler,
  .argument_list = { CONSOLE_ENUM_ARG(ota_options),
                     CONSOLE_ARG_STRING,
                     CONSOLE_ARG_END }
};

sl_status_t sli_setkey_handler(console_args_t *arguments);
static const char *sli_setkey_arg_help[]                      = {};
static const console_descriptive_command_t sli_setkey_command = {
  .description   = "Set GW key command",
  .argument_help = sli_setkey_arg_help,
  .handler       = sli_setkey_handler,
  .argument_list = { CONSOLE_ARG_STRING, CONSOLE_ARG_END }
};

sl_status_t sli_ip_route_show(console_args_t *arguments);
static const char *sli_ip_route_arg_help[]                      = {};
static const console_descriptive_command_t sli_ip_route_command = {
  .description   = "IP route command",
  .argument_help = sli_ip_route_arg_help,
  .handler       = sli_ip_route_show,
  .argument_list = { CONSOLE_ARG_STRING, CONSOLE_ARG_END }
};

sl_status_t sli_tls_connect_handler(console_args_t *arguments);
static const char *sli_tls_arg_help[]                      = {};
static const console_descriptive_command_t sli_tls_command = {
  .description   = "tls command",
  .argument_help = sli_tls_arg_help,
  .handler       = sli_tls_connect_handler,
  .argument_list = { CONSOLE_ARG_STRING, CONSOLE_ARG_INT, CONSOLE_ARG_END }
};

const console_database_t console_command_database = {
  CONSOLE_DATABASE_ENTRIES({ "help", &sli_help_command },
                           { "dummy", &sli_dummy_command },
                           { "setkey", &sli_setkey_command },
                           { "tls", &sli_tls_command },
                           { "ota", &sli_ota_command },
                           { "route", &sli_ip_route_command })
};

/****************************************************************************/
/*                            PRIVATE FUNCTIONS                             */
/****************************************************************************/

void sli_cli_thread(const void *unused)
{
  UNUSED_PARAMETER(unused);
  console_args_t args;
  const console_descriptive_command_t *command;

  printf("CLI start \n\r");

  console_line_ready = 0;

  while (1) {
    printf("\r\n> \r\n");

    while (!console_line_ready) {
      console_process_uart_data();
      osDelay(20);
    }

    sl_status_t result =
      console_process_buffer(&console_command_database, &args, &command);

    if (result == SL_STATUS_OK) {
      printf("\nProcessing command\n");
      if (command->handler) {
        printf("\r\n");
        uint32_t start_time = osKernelGetTickCount();
        result              = command->handler(&args);
        uint32_t duration   = osKernelGetTickCount() - start_time;
        sli_print_status(result, duration);
      }
    } else if (result == SL_STATUS_COMMAND_IS_INVALID) {
      printf("\r\nArgs: ");
      sli_print_command_args(command);
      sli_print_status(SL_STATUS_INVALID_PARAMETER, 0);
    } else {
      printf("\r\nNot supported\r\n");
    }
    console_line_ready = 0;
  }
}

void sli_print_status(sl_status_t status, uint32_t duration)
{
  printf("\r\n0x%05lX: (%lums) %s\r\n",
         status,
         duration,
         (status == SL_STATUS_OK) ? "Success" : "");
}

static void sli_print_enum_options(uint8_t enum_index)
{
  for (int b = 0; console_argument_types[enum_index][b] != NULL;
       /* Increment occurs in internal logic */) {
    printf("%s", console_argument_types[enum_index][b]);
    if (console_argument_types[enum_index][++b]) {
      printf("|");
    }
  }
}

static void sli_print_command_args(const console_descriptive_command_t *command)
{
  bool is_optional = false;
  for (int a = 0; command->argument_list[a] != CONSOLE_ARG_END; ++a) {
    if (command->argument_list[a] & CONSOLE_ARG_OPTIONAL) {
      char option_char[2] = { (char) command->argument_list[a]
                              & CONSOLE_ARG_OPTIONAL_CHARACTER_MASK,
                              0 };
      printf("[-");
      printf("%s", option_char);
      printf(" ");
      is_optional = true;
      continue;
    } else if (command->argument_list[a] & CONSOLE_ARG_ENUM) {
      printf("{");
      uint8_t enum_index =
        command->argument_list[a] & CONSOLE_ARG_ENUM_INDEX_MASK;
      sli_print_enum_options(enum_index);
      printf("}");
    } else {
      printf("<");
      if (command->argument_help && command->argument_help[a]) {
        printf("%s", command->argument_help[a]);
      } else {
        printf(console_argument_type_strings[command->argument_list[a]
                                             & CONSOLE_ARG_ENUM_INDEX_MASK]);
      }
      printf(">");
    }
    if (is_optional) {
      printf("] ");
      is_optional = false;
    } else {
      printf(" ");
    }
  }
}

extern void sl_tcp_client_set_ip_start(uint8_t *ip, uint16_t port);
extern void sl_tcp_client_enable(int en);
sl_status_t sli_tls_connect_handler(console_args_t *arguments)
{
  uint8_t *p = (uint8_t *) arguments->arg[0];
  uint32_t port = (uint32_t)arguments->arg[1];
  if (p) {
    sl_tcp_client_set_ip_start(p, (uint16_t)port);
    sl_tcp_client_enable(1);
  }
  return SL_STATUS_OK;
}

// setkey ABCD11111335353532
extern uint8_t networkKey[16];
extern void sec0_set_key(uint8_t *netkey);
sl_status_t sli_setkey_handler(console_args_t *arguments)
{
  uint8_t *p = (uint8_t *) arguments->arg[0];
  uint8_t key[16];
  int n = strlen((char *) p);

  printf("arg: %s, len: %d", p, n);
  if (n != 32) {
    return -1;
  }
  int j = 0;
  for (int i = 0; i < n; i += 2) {
    uint8_t h = ((p[i] & 0x0F) << 4) | (p[i + 1] & 0x0F);
    key[j++]  = h;
  }

  memcpy(networkKey, key, 16);
  sec0_set_key(networkKey);
  return SL_STATUS_OK;
}

sl_status_t sli_ip_route_show(console_args_t *arguments)
{
  (void) arguments;
  printf("\r\n--- lwIP Route Table ---\r\n");
  // Print default routers (next hops)
  printf("--- IPv6 Default Routers (Next Hops) ---\r\n");
  struct nd6_router_list_entry *router;
  for (uint8_t i = 0; i < LWIP_ND6_NUM_ROUTERS; i++) {
    if (default_router_list[i].neighbor_entry == NULL) {
      continue;
    }
    router = &default_router_list[i];
    char buf[40];
    ip6addr_ntoa_r(&(router->neighbor_entry->next_hop_address),
                   buf,
                   sizeof(buf));
    printf("Next Hop: %s, Netif: %c%c (idx=%d)\r\n",
           buf,
           router->neighbor_entry->netif->name[0],
           router->neighbor_entry->netif->name[1],
           router->neighbor_entry->netif->num);
  }
  // Print neighbor cache entries
  printf("--- IPv6 Neighbor Cache Entries ---\r\n");
  for (uint8_t i = 0; i < LWIP_ND6_NUM_NEIGHBORS; i++) {
    struct nd6_neighbor_cache_entry *entry = &neighbor_cache[i];
    if (entry->isrouter == 0 && entry->state == ND6_NO_ENTRY) {
      continue;
    }
    char llbuf[32];
    snprintf(llbuf,
             sizeof(llbuf),
             "%02x:%02x:%02x:%02x:%02x:%02x",
             entry->lladdr[0],
             entry->lladdr[1],
             entry->lladdr[2],
             entry->lladdr[3],
             entry->lladdr[4],
             entry->lladdr[5]);
    printf("LLAddr: %s, ", llbuf);
    printf("State: %d, Netif: %c%c (idx=%d), IsRouter: %d\r\n",
           entry->state,
           entry->netif ? entry->netif->name[0] : '-',
           entry->netif ? entry->netif->name[1] : '-',
           entry->netif ? entry->netif->num : -1,
           entry->isrouter);
  }
  // Print destination cache entries
  printf("--- IPv6 Destination Cache Entries ---\r\n");
  for (uint8_t i = 0; i < LWIP_ND6_NUM_DESTINATIONS; i++) {
    struct nd6_destination_cache_entry *entry = &destination_cache[i];
    if (entry->pmtu == 0) {
      continue;
    }
    char destbuf[40], nhbuf[40];
    ip6addr_ntoa_r(&(entry->destination_addr), destbuf, sizeof(destbuf));
    ip6addr_ntoa_r(&(entry->next_hop_addr), nhbuf, sizeof(nhbuf));
    printf("Dest: %s, Next Hop: %s, PMTU: %u\r\n", destbuf, nhbuf, entry->pmtu);
  }
  return SL_STATUS_OK;
}
// Command list functions
sl_status_t sli_help_command_handler(console_args_t *arguments)
{
  UNUSED_PARAMETER(arguments);
  for (uint8_t a = 0; a < console_command_database.length; ++a) {
    printf("\r\n");
    printf("%s", console_command_database.entries[a].key);
    printf("  ");
    sli_print_command_args(
      (console_descriptive_command_t *) console_command_database.entries[a]
      .value);
    printf("\r\n   ");
    printf(
      ((console_descriptive_command_t *) console_command_database.entries[a]
       .value)
      ->description);
  }
  return SL_STATUS_OK;
}

sl_status_t sli_dummy_handler(console_args_t *arguments)
{
  (void) arguments;
  printf("Dummy function \r\n");

  return SL_STATUS_OK;
}

sl_status_t sli_ota_handler(console_args_t *arguments)
{
  switch ((int32_t) arguments->arg[0]) {
    case OTA_BRIDGE:
      sl_bridge_ota((char *) arguments->arg[1]);
      break;

    case OTA_CONTROLLER:
      break;

    case OTA_STOP:
      break;

    default:
      LOG_PRINTF("Invalid arguments \r\n");
  }
  return SL_STATUS_OK;
}

/****************************************************************************/
/*                            PUBLIC FUNCTIONS                              */
/****************************************************************************/

void sl_cli_init(void)
{
  osThreadNew((osThreadFunc_t) sli_cli_thread,
              NULL,
              &sli_cli_thread_attributes);
}
