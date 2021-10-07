#ifndef SAVE_H_
#define SAVE_H_

#include "pico/stdlib.h"

void flash_erase();
void flash_write(uint8_t data[], uint32_t size);
void flash_read(uint8_t data[], uint32_t size);

bool verify_flash();
#endif // SAVE_H_