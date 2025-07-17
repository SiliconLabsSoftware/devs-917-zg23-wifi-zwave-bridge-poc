/*******************************************************************************
 * @file  sl_bridge_temp_assoc.c
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

#include "lib/list.h"
#include "lib/memb.h"

#include "sl_common_log.h"
#include "sl_common_type.h"
#include "sl_rd_types.h"
#include "sl_sleeptimer.h"
#include "sl_gw_info.h"
#include "sl_bridge_temp_assoc.h"
#include "ZW_controller_bridge_api.h"
#include "modules/sl_rd_data_store.h"
#include "utls/zgw_nodemask.h"
#include "utls/sl_ipnode_utils.h"
#include "utls/ipv6_utils.h"

#include "sl_bridge.h"
#include "sl_router_events.h"
#include "sl_status.h"

sl_status_t zw_zip_post_event(uint32_t event, void *data);

extern bridge_state_t bridge_state;

/* Timeout before retrying ZW_SetSlaveLearnMode */
#define TEMP_ASSOC_VIRTUAL_ADD_RETRY_TIMEOUT 1000
#define TEMP_ASSOC_VIRTUAL_ADD_RETRY_MAX     10

/* Cooldown period after each successful virtual node add */
/* 2 sec due to TO#3682 */
#define TEMP_ASSOC_VIRTUAL_ADD_COOLDOWN_DELAY 2000

static void temp_assoc_add_virtual_nodes(sl_sleeptimer_timer_handle_t *h,
                                         void *user);
static void temp_assoc_add_to_association_table(temp_association_t *new_a);
static temp_association_t *temp_assoc_get_new_or_reuse_oldest(void);
static temp_association_t *temp_assoc_lookup_by_zip_client(uint8_t was_dtls);
static BOOL temp_assoc_setup_and_save(temp_association_t *new_assoc);

LIST(temp_association_table);
MEMB(temp_association_pool, temp_association_t, MAX_TEMP_ASSOCIATIONS);

// Array with virtual node ids (for temp associations) and count of active elements
static nodeid_t temp_assoc_virtual_nodeids[MAX_CLASSIC_TEMP_ASSOCIATIONS];
// LR virtual node IDs are always 4002 - 4005
static nodeid_t lr_temp_assoc_virtual_nodeids[MAX_LR_TEMP_ASSOCIATIONS] =
{ 4002, 4003, 4004, 4005 };
static uint8_t temp_assoc_virtual_nodeid_count;
static uint8_t lr_temp_assoc_virtual_nodeid_count = MAX_LR_TEMP_ASSOCIATIONS;
static int temp_assoc_add_virtual_node_retry_count;
static int temp_assoc_next_nodeid_idx;
static int lr_temp_assoc_next_nodeid_idx;

// Global variable shared
temp_assoc_fw_lock_t temp_assoc_fw_lock;

void temp_assoc_init(void)
{
  list_init(temp_association_table);
  memb_init(&temp_association_pool);

  temp_assoc_virtual_nodeid_count = 0;
  memset(temp_assoc_virtual_nodeids, 0, sizeof(temp_assoc_virtual_nodeids));
  memset(&temp_assoc_fw_lock, 0, sizeof(temp_assoc_fw_lock));

  temp_assoc_add_virtual_node_retry_count = 0;
  temp_assoc_next_nodeid_idx              = 0;
  lr_temp_assoc_next_nodeid_idx           = 0;
}

void temp_assoc_resume_init(void)
{
  LOG_PRINTF("temp_assoc_resume_init\n");
  /* Only resume if no retry timer is running
   * (a retry timer could be running if we are below the max retry count)
   */
  if (temp_assoc_add_virtual_node_retry_count
      >= TEMP_ASSOC_VIRTUAL_ADD_RETRY_MAX) {
    temp_assoc_add_virtual_node_retry_count = 0;
    temp_assoc_add_virtual_nodes(NULL, NULL);
  }
}

/* Documented in header file */
void temp_assoc_persist_virtual_nodeids(void)
{
  rd_datastore_persist_virtual_nodes(temp_assoc_virtual_nodeids,
                                     temp_assoc_virtual_nodeid_count);
}

/* Documented in header file */
void temp_assoc_unpersist_virtual_nodeids(void)
{
  temp_assoc_virtual_nodeid_count =
    rd_datastore_unpersist_virtual_nodes(temp_assoc_virtual_nodeids,
                                         MAX_CLASSIC_TEMP_ASSOCIATIONS);
  LOG_PRINTF("temp_assoc_virtual_nodeids (count=%d): %u %u %u ... %u\n",
             temp_assoc_virtual_nodeid_count,
             temp_assoc_virtual_nodeids[0],
             temp_assoc_virtual_nodeids[1],
             temp_assoc_virtual_nodeids[2],
             temp_assoc_virtual_nodeids[MAX_CLASSIC_TEMP_ASSOCIATIONS - 1]);
}

/* Documented in header file */
temp_association_t *temp_assoc_create(uint8_t was_dtls)
{
  temp_association_t *a = NULL;
  /* Check if we already have a temporary association for the current Z/IP
   * client (i.e. matching the srcipaddr, srcport and sEndpoint of the package
   * in BACKUP_UIP_IP_BUF).
   */
  a = temp_assoc_lookup_by_zip_client(was_dtls);

  if (!a) {
    a = temp_assoc_get_new_or_reuse_oldest();

    if (a) {
      a->was_dtls = was_dtls;

      if (!temp_assoc_setup_and_save(a)) {
        /* Setup failed and "a" was returned to the pool */
        a = NULL;
      }
    } else {
      /* Can happen if a non-responding SIS causes virtual node creation to fail */
      DBG_PRINTF("Failed to alloc or recycle a temp_assoc_t struct.\n");
    }
  }

  return a;
}

void sl_temp_assoc_fw_lock_release_on_timeout(sl_sleeptimer_timer_handle_t *t,
                                              void *u)
{
  (void) t;
  temp_assoc_fw_lock_release_on_timeout(u);
}

/* Documented in header file */
void temp_assoc_register_fw_lock(temp_association_t *a)
{
  DBG_PRINTF("Setting fw flag for temp association %p %d->ANY\n",
             a,
             a->virtual_id);
  if (temp_assoc_fw_lock.locked_a) {
    DBG_PRINTF("Previous fw locked temp association was %p %d->ANY\n",
               temp_assoc_fw_lock.locked_a,
               temp_assoc_fw_lock.locked_a->virtual_id);
  }
  temp_assoc_fw_lock.locked_a = a;
  sl_sleeptimer_start_timer_ms(&temp_assoc_fw_lock.reset_fw_timer,
                               60000,
                               sl_temp_assoc_fw_lock_release_on_timeout,
                               a,
                               1,
                               0);
}

/* Documented in header file */
void temp_assoc_fw_lock_release_on_timeout(void *user)
{
  /* Note that the temp association passed with the "user" data parameter is
   * currently not used for anything else than debug and warning message. We
   * unconditionally clear the firmware lock even if it is for another temp
   * association (don't know if the code flow can allow this)
   */
  temp_association_t *a = user;
  if (temp_assoc_fw_lock.locked_a != a) {
  }
  temp_assoc_fw_lock.locked_a = 0;
}

/* Documented in header file */
temp_association_t *temp_assoc_lookup_by_virtual_nodeid(nodeid_t virtual_nodeid)
{
  for (temp_association_t *a = list_head(temp_association_table); a != NULL;
       a                     = list_item_next(a)) {
    if (virtual_nodeid == a->virtual_id) {
      return a;
    }
  }

  return NULL;
}

/* Documented in header file */
void temp_assoc_delayed_add_virtual_nodes(void)
{
  LOG_PRINTF("temp_assoc_delayed_add_virtual_nodes\n");
  temp_assoc_add_virtual_nodes(NULL, NULL);
}

void temp_assoc_print_association_table(void)
{
  LOG_PRINTF("Number of temporary associations: %d\n",
             list_length(temp_association_table));

  uint8_t line_no = 1;
  for (temp_association_t *ta = list_head(temp_association_table); ta != NULL;
       ta                     = list_item_next(ta), line_no++) {
    LOG_PRINTF("TA: %d, %d\n", ta->resource_endpoint, ta->virtual_id);
    print_association_list_line(line_no,
                                ta->resource_endpoint,
                                uip_ntohs(ta->resource_port),
                                &ta->resource_ip,
                                ta->virtual_id,
                                0, // No virtual endpoint for temp associations
                                0, // No HAN node id for temp associations
                                0, // No HAN endpoint for for temp associations
                                "temp");
  }
}

void temp_assoc_print_virtual_node_ids(void)
{
  // Each nodeid is printed with two decimal characters and a space (len=3)
  char nodeid_str[(MAX_CLASSIC_TEMP_ASSOCIATIONS * 3) + 1] = { 0 };

  int pos = 0;
  for (uint8_t i = 0; i < temp_assoc_virtual_nodeid_count; i++) {
    pos += sprintf(nodeid_str + pos, "%02d ", temp_assoc_virtual_nodeids[i]);
  }
  LOG_PRINTF("Virtual nodeids for temporary associations (decimal): %s\n",
             nodeid_str);
}

/**
 * Called during initialization to load virtual node ids for temp associations
 *
 * If there are currently less than MAX_TEMP_ASSOCIATIONS virtual nodes
 * allocated to temporary association this function will be called repeatedly by
 * the callback from ZW_SetSlaveLearnMode() until MAX_TEMP_ASSOCIATIONS virtual
 * nodes have been created.
 *
 * @param user User data for timer callback. Not used here.
 */
static void temp_assoc_add_virtual_nodes(sl_sleeptimer_timer_handle_t *h,
                                         void *user)
{
  (void) h;
  (void) user;
  if (temp_assoc_virtual_nodeid_count < MAX_CLASSIC_TEMP_ASSOCIATIONS) {
    if (temp_assoc_add_virtual_node_retry_count
        < TEMP_ASSOC_VIRTUAL_ADD_RETRY_MAX) {
      temp_assoc_add_virtual_node_retry_count++;
    } else {
      printf("Giving up retrying to create virtual node");
      bridge_state = initfail;
      zw_zip_post_event(ZIP_EVENT_BRIDGE_INITIALIZED, 0);
    }
  } else {
    bridge_state = initialized;
    zw_zip_post_event(ZIP_EVENT_BRIDGE_INITIALIZED, 0);
  }
}

void temp_assoc_virtual_node_clean(void)
{
  temp_assoc_virtual_nodeid_count = 0;
  memset(temp_assoc_virtual_nodeids, 0, sizeof(temp_assoc_virtual_nodeids));
}
void temp_assoc_virtual_get_from_controller(nodeid_t newID)
{
  temp_assoc_virtual_nodeids[temp_assoc_virtual_nodeid_count] = newID;
  temp_assoc_virtual_nodeid_count++;
  temp_assoc_persist_virtual_nodeids();
}

/**
 * Add temporary association to the temp association table
 *
 * If the provided temp association new_a is already in the association table it
 * will be returned to the association pool. I.e. don't reference the pointer
 * after this call.
 *
 * @param new_a temporary association to add
 */
static void temp_assoc_add_to_association_table(temp_association_t *new_a)
{
  LOG_PRINTF("temp_assoc_add_to_association_table: %d\n",
             new_a->virtual_id_static);
  /* Check for duplicates before adding. */
  for (temp_association_t *a = list_head(temp_association_table); a != NULL;
       a                     = list_item_next(a)) {
    // Compare the structs starting from element "virtual_id_static" (i.e. skipping the next pointer and virtual_id)
    if (0 == memcmp(&new_a->virtual_id_static,
                    &a->virtual_id_static,
                    sizeof(temp_association_t)
                    - offsetof(temp_association_t, virtual_id_static))) {
      // New association is already in the table. Return item to pool.
      memb_free(&temp_association_pool, new_a);

      LOG_PRINTF("temp_assoc_add_to_association_table: %d existed\n",
                 new_a->virtual_id_static);
      return;
    }
  }
  // New association was not found in the table. Add it.
  list_add(temp_association_table, new_a);
}

static bool check_available_temp_assoc()
{
  if (is_lr_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())))
      && (lr_temp_assoc_next_nodeid_idx < MAX_LR_TEMP_ASSOCIATIONS)) {
    DBG_PRINTF("%d lr temp associations are currently allocated (leaving %d "
               "unallocated)\n",
               lr_temp_assoc_next_nodeid_idx + 1,
               MAX_LR_TEMP_ASSOCIATIONS - lr_temp_assoc_next_nodeid_idx - 1);
    return true;
  } else if (is_classic_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())))
             && (temp_assoc_next_nodeid_idx < MAX_CLASSIC_TEMP_ASSOCIATIONS)) {
    DBG_PRINTF("%d classic temp associations are currently allocated (leaving "
               "%d unallocated)\n",
               temp_assoc_next_nodeid_idx + 1,
               MAX_CLASSIC_TEMP_ASSOCIATIONS - temp_assoc_next_nodeid_idx - 1);
    return true;
  }
  return false;
}

/**
 * Allocate a new temporary association struct of reuse the oldest one if the
 * pool is empty.
 *
 * The pool of temporary associations holds MAX_TEMP_ASSOCIATIONS elements. As
 * long as there are unused elements in the pool they will be allocated here.
 *
 * When the pool is empty return the oldest previously allocated element not
 * locked for an ongoing firmware update. (since new elements are added to the
 * end of the list the oldest one is the first element).
 *
 * @return temp_association_t* the new or reused allocation structure
 */
static temp_association_t *temp_assoc_get_new_or_reuse_oldest(void)
{
  temp_association_t *a    = NULL;
  uint8_t temp_assoc_count = list_length(temp_association_table);

  if (temp_assoc_count == 0) {
    LOG_PRINTF("Initializing temp_assoc_fw_lock\n");
    temp_assoc_fw_lock.locked_a = 0;
    sl_sleeptimer_stop_timer(&temp_assoc_fw_lock.reset_fw_timer);
  }

  if (check_available_temp_assoc()) {
    DBG_PRINTF("Allocating new temp association from pool...\n");
    a = (temp_association_t *) memb_alloc(&temp_association_pool);
    memset(a, 0, sizeof(temp_association_t));

    if ((lr_temp_assoc_next_nodeid_idx < lr_temp_assoc_virtual_nodeid_count)
        && is_lr_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())))) {
      DBG_PRINTF("Next LR virtual node ID for temporary association %d\n",
                 lr_temp_assoc_virtual_nodeids[lr_temp_assoc_next_nodeid_idx]);
      a->virtual_id_static =
        lr_temp_assoc_virtual_nodeids[lr_temp_assoc_next_nodeid_idx];
      a->is_long_range = true;
      lr_temp_assoc_next_nodeid_idx++;
    } else if ((temp_assoc_next_nodeid_idx < temp_assoc_virtual_nodeid_count)
               && is_classic_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())))) {
      DBG_PRINTF("Next virtual node ID for temporary association %d\n",
                 temp_assoc_virtual_nodeids[temp_assoc_next_nodeid_idx]);
      a->virtual_id_static =
        temp_assoc_virtual_nodeids[temp_assoc_next_nodeid_idx];
      a->is_long_range = false;
      temp_assoc_next_nodeid_idx++;
    } else {
      a->virtual_id_static = 0;
    }
    DBG_PRINTF("Assigning virtual node %d to new temporary association %p.\n",
               a->virtual_id_static,
               a);

    return a;
  }

  /* No unused associations remain in the pool. We need to re-use one of the
   * temp associations previously allocated. The oldest ones are at the
   * beginning of the association list. Get the first one not locked for
   * firmware update.
   */
  for (a = list_head(temp_association_table); a != NULL;
       a = list_item_next(a)) {
    DBG_PRINTF(
      "Checking if temporary association %p (virt nodeid: %d) is locked...\n",
      a,
      a->virtual_id);
    if ((a != temp_assoc_fw_lock.locked_a)
        && a->is_long_range == is_lr_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())))) {
      DBG_PRINTF("Not locked. Recycling association for virtual nodeid: %d\n",
                 a->virtual_id);
      list_remove(temp_association_table, a);
      return a;
    } else {
      DBG_PRINTF("Locked for firmware update. Skipping to next.\n");
    }
  }

  return NULL; /* Alloc a new temp_assoc if none is found */
}

/**
 * Lookup a temporary association for the Z/IP client that sent the package
 * currently held in the uIP buffer.
 *
 * \note In addition to the function parameter the lookup is also using the
 * content of the most recently received Z/IP package (currently in the uIP
 * backup buffer).
 *
 * @param was_dtls DTLS flag on the association
 * @return         The matching temp association
 * @return         NULL if not found.
 */
static temp_association_t *temp_assoc_lookup_by_zip_client(uint8_t was_dtls)
{
  temp_association_t *a = NULL;

  for (a = list_head(temp_association_table); a != NULL;
       a = list_item_next(a)) {
    LOG_PRINTF("IA: p=%d, e=%d, s=%d\n",
               a->resource_port,
               a->resource_endpoint,
               a->was_dtls);
    uip_debug_ipaddr_print(&a->resource_ip);
    LOG_PRINTF("\n");
    if (uip_ipaddr_cmp(&a->resource_ip, &(sl_uip_buf_src_addr()))) {
      a->resource_endpoint = sl_uip_buf_lendpoint();
      a->resource_port     = sl_uip_buf_src_port();
      a->was_dtls          = was_dtls;
      a->is_long_range     = is_lr_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())));

      DBG_PRINTF("temp_assoc_lookup_by_zip_client() found: %p, TIME %ld\n", a, osKernelGetTickCount());
      return a;
    }
  }
  DBG_PRINTF("temp_assoc_lookup_by_zip_client() NOT found\n");
  return NULL;
}

/**
 * Finish the setup of a temporary association
 *
 * Will set the association properties from the Z/IP package currently in the
 * uIP backup buffer and assign a virtual node id to the association (unless the
 * Z/IP package is form the unsolicited destination in which case the nodeid of
 * the gateway will be used).
 *
 * Register the association in the association table and persist the list of
 * virtual node ids.
 *
 * @param new_assoc Temporary association struct to setup.
 * @return TRUE if the temporary association was setup with success
 * @return FALSE if error (the association structure has been returned to the
 *         pool)
 */
static BOOL temp_assoc_setup_and_save(temp_association_t *new_assoc)
{
  // When we get here a static virtual id should already have been
  // assigned to the association
  ASSERT(new_assoc->virtual_id_static);

  new_assoc->resource_port     = sl_uip_buf_src_port();
  new_assoc->resource_endpoint = sl_uip_buf_rendpoint();
  uip_ipaddr_copy(&(new_assoc->resource_ip), &(sl_uip_buf_src_addr()));
  new_assoc->is_long_range = is_lr_node(sl_node_of_ip(&(sl_uip_buf_dst_addr())));
  {
    new_assoc->virtual_id = new_assoc->virtual_id_static;
  }

  if (new_assoc->virtual_id == 0) {
    /* This should never happen */
    ERR_PRINTF("Virtual node id not assigned for temporary association %p.\n",
               new_assoc);
    memb_free(&temp_association_pool, new_assoc);
    return FALSE;
  } else {
    temp_assoc_add_to_association_table(new_assoc);
    if (new_assoc->is_long_range == false) {
      temp_assoc_persist_virtual_nodeids();
    }
    return TRUE;
  }
}

/* Helper functions to inspect temp association table from debugger */
temp_association_t *temp_assoc_head()
{
  return (temp_association_t *) list_head(temp_association_table);
}

temp_association_t *temp_assoc_next(temp_association_t *a)
{
  return (temp_association_t *) list_item_next(a);
}
