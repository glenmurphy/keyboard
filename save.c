// From https://github.com/raspberrypi/pico-examples/blob/master/flash/program/flash_program.c

#include "pico/stdlib.h"
#include <string.h> // for memset
#include "hardware/flash.h"
#include "save.h"

// We're going to erase and reprogram a region 256k from the start of flash.
// Once done, we can access this at XIP_BASE + 256k.
#define FLASH_TARGET_OFFSET (256 * 1024)

const uint8_t *flash_target_contents = (const uint8_t *) (XIP_BASE + FLASH_TARGET_OFFSET);

void flash_erase() {
  flash_range_erase(FLASH_TARGET_OFFSET, FLASH_SECTOR_SIZE);
}

void flash_write(uint8_t data[], uint32_t size) {
  flash_erase();

  uint8_t page[FLASH_PAGE_SIZE];
  memset(page, 0, FLASH_PAGE_SIZE);
  for (int i = 0; i < size && i < FLASH_PAGE_SIZE; i++) {
    page[i] = data[i];
  }
  flash_range_program(FLASH_TARGET_OFFSET, page, FLASH_PAGE_SIZE);
}

void flash_read(uint8_t data[], uint32_t size) {
  for (int i = 0; i < size; i++) {
    data[i] = flash_target_contents[i];//*(uint8_t *) (flash_target_contents + i);
  }
}

bool verify_flash() {
  uint8_t data[64];
  for (int i = 0; i < 64; i++) {
    data[i] = i;
  }
  flash_write(data, 64);

  uint8_t read_data[64];
  memset(read_data, 0, 64);
  flash_read(read_data, 64);
  
  for (int i = 0; i < 64; i++) {
    if (data[i] != read_data[i]) {
      return false;
    }
  }
  return true;
}