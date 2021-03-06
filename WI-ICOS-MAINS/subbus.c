/* subbus.c for Atmel Studio
 */
#include <string.h>
#include "subbus.h"

static subbus_driver_t *drivers[SUBBUS_MAX_DRIVERS];
static int n_drivers = 0;

/** @return true on error.
 * Possible errors include too many drivers or drivers not in ascending order.
 */
bool subbus_add_driver(subbus_driver_t *driver) {
  if ((n_drivers >= SUBBUS_MAX_DRIVERS) ||
      ((n_drivers > 0) && (drivers[n_drivers-1]->high > driver->low)) ||
      driver->high < driver->low )
    return true;
  drivers[n_drivers++] = driver;
  return false;
}

void subbus_reset(void) {
  int i;
  for (i = 0; i < n_drivers; ++i) {
    if (drivers[i]->reset) {
      (*(drivers[i]->reset))();
    }
  }
}

void subbus_poll(void) {
  int i;
  for (i = 0; i < n_drivers; ++i) {
    if (drivers[i]->poll) {
      (*drivers[i]->poll)();
    }
  }
}

/**
 * @return non-zero on success (acknowledge)
 */
int subbus_read( uint16_t addr, uint16_t *rv ) {
  int i;
  for (i = 0; i < n_drivers; ++i) {
    if (addr < drivers[i]->low) return 0;
    if (addr <= drivers[i]->high) {
      uint16_t offset = addr-drivers[i]->low;
      subbus_cache_word_t *cache = &drivers[i]->cache[offset];
      if (cache->readable) {
        *rv = cache->cache;
        cache->was_read = true;
        if (cache->dynamic && drivers[i]->sb_action)
          drivers[i]->sb_action();
        return 1;
      }
    }
  }
  *rv = 0;
  return 0;
}

/**
 * @return non-zero on success (acknowledge)
 */
int subbus_write( uint16_t addr, uint16_t data) {
  int i;
  for (i = 0; i < n_drivers; ++i) {
    if (addr < drivers[i]->low) return 0;
    if (addr <= drivers[i]->high) {
      uint16_t offset = addr-drivers[i]->low;
      subbus_cache_word_t *cache = &drivers[i]->cache[offset];
      if (cache->writable) {
        cache->wvalue = data;
        cache->written = true;
        if (cache->dynamic && drivers[i]->sb_action)
          drivers[i]->sb_action();
        return 1;
      }
    }
  }
  return 0;
}

void set_fail(uint16_t arg) {
#if defined(SUBBUS_FAIL_ADDR)
  subbus_write(SUBBUS_FAIL_ADDR, arg);
#endif
}

#if SUBBUS_INTERRUPTS
extern volatile uint8_t subbus_intr_req;
void init_interrupts(void);
int intr_attach(int id, uint16_t addr);
int intr_detach( uint16_t addr );
void intr_service(void);
#endif

static subbus_cache_word_t sb_base_cache[SUBBUS_INSTID_ADDR+1] = {
  { 0, 0, 0, 0, 0, 0, 0 }, // Reserved zero address
  { 0, 0, 0, 0, 0, 0, 0} , // INTA
  { SUBBUS_BOARD_ID, 0, 1, 0, 0, 0, 0 },  // Board ID (SUBBUS_BDID_ADDR)
  { SUBBUS_BOARD_BUILD_NUM, 0, 1, 0, 0, 0, 0 }, // Build number (SUBBUS_BLDNO_ADDR)
  { SUBBUS_BOARD_SN, 0, 1, 0, 0, 0, 0 }, // Build number (SUBBUS_BDSN_ADDR)
  { SUBBUS_BOARD_INSTRUMENT_ID, 0, 1, 0, 0, 0, 0 } // Build number (SUBBUS_BDSN_ADDR)
};

subbus_driver_t sb_base = { 0, SUBBUS_INSTID_ADDR, sb_base_cache, 0, 0, 0, false };

static subbus_cache_word_t sb_fail_sw_cache[SUBBUS_SWITCHES_ADDR-SUBBUS_FAIL_ADDR+1] = {
  { 0, 0, 1, 0, 1, 0, 0 }, // Fail Register
  { 0, 0, 1, 0, 0, 0, 0 }  // Switches
};

static void sb_fail_sw_reset() {
  sb_fail_sw_cache[0].cache = 0;
}

static void sb_fail_sw_poll() {
  if (sb_fail_sw_cache[0].written) {
    sb_fail_sw_cache[0].cache = sb_fail_sw_cache[0].wvalue;
    sb_fail_sw_cache[0].written = false;
  }
}

subbus_driver_t sb_fail_sw = { SUBBUS_FAIL_ADDR, SUBBUS_SWITCHES_ADDR,
    sb_fail_sw_cache, sb_fail_sw_reset, sb_fail_sw_poll, 0, false };


/**
 * If a value has been written to the specified address since the
 * last call to this function, the new value is written at the
 * value address.
 * @param addr The cache address
 * @param value Pointer where value may be written
 * @return true if a value has been written to this address.
 */
bool subbus_cache_iswritten(subbus_driver_t *drv, uint16_t addr, uint16_t *value) {
  if (addr >= drv->low && addr <= drv->high) {
    return sb_cache_iswritten(drv->cache, addr-drv->low, value);
  }
  return false;
}

bool sb_cache_iswritten(subbus_cache_word_t *cache, uint16_t offset, uint16_t *value) {
  subbus_cache_word_t *word = &cache[offset];
  if (word->writable && word->written) {
    *value = word->wvalue;
    word->written = false;
    return true;
  }
  return false;
}


/**
 * This function differs from subbus_write() in that it directly
 * updates the cache value. subbus_write() is specifically for
 * write originating from the control port. subbus_cache_update() is
 * used by internal functions for storing data acquired from
 * peripherals, or for storing values written from the control
 * port after verifying them.
 * @param drv The driver structure
 * @param addr The cache address
 * @param data The value to be written
 * @return true on success
 */
bool subbus_cache_update(subbus_driver_t *drv, uint16_t addr, uint16_t data) {
  if (addr >= drv->low && addr <= drv->high) {
    return sb_cache_update(drv->cache, addr-drv->low, data);
  }
  return false;
}

bool sb_cache_update(subbus_cache_word_t *cache, uint16_t offset, uint16_t data) {
  subbus_cache_word_t *word = &cache[offset];
  if (word->readable) {
    word->cache = data;
    word->was_read = false;
    return true;
  }
  return false;
}

bool subbus_cache_was_read(subbus_driver_t *drv, uint16_t addr) {
  if (addr >= drv->low && addr <= drv->high) {
    return sb_cache_was_read(drv->cache, addr-drv->low);
  }
  return false;
}

bool sb_cache_was_read(subbus_cache_word_t *cache, uint16_t offset) {
  return cache[offset].was_read;
}

extern subbus_driver_t sb_board_desc;

static subbus_cache_word_t board_desc_cache[2] = {
  { 0, 0, true, false, false, false, false },
  { 0, 0, true, false, false, false, true }
};

static struct board_desc_t {
  const char *desc;
  int cp;
  int nc;
} board_desc;

static void board_desc_init(void) {
  board_desc.desc = SUBBUS_BOARD_REV;
  board_desc.cp = 0;
  board_desc.nc = strlen(board_desc.desc)+1; // Include the trailing NUL
  subbus_cache_update(&sb_board_desc, SUBBUS_DESC_FIFO_SIZE_ADDR, (board_desc.nc+1)/2);
  subbus_cache_update(&sb_board_desc, SUBBUS_DESC_FIFO_ADDR,
    (board_desc.desc[0] & 0xFF) + (board_desc.desc[1]<<8));
}

static void board_desc_action(void) {
  if (board_desc_cache[1].was_read) {
    board_desc.cp += 2;
    if (board_desc.cp >= board_desc.nc) {
      board_desc.cp = 0;
    }
  }
  subbus_cache_update(&sb_board_desc, SUBBUS_DESC_FIFO_SIZE_ADDR,
    ((board_desc.nc-board_desc.cp)+1)/2);
  subbus_cache_update(&sb_board_desc, SUBBUS_DESC_FIFO_ADDR,
    (board_desc.desc[board_desc.cp] & 0xFF) + (board_desc.desc[board_desc.cp+1]<<8));
}

subbus_driver_t sb_board_desc = {
  SUBBUS_DESC_FIFO_SIZE_ADDR, SUBBUS_DESC_FIFO_ADDR,
  board_desc_cache, board_desc_init, 0, board_desc_action,
  false };
