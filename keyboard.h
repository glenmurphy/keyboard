#include "pico/stdlib.h" // bool, uint8_t

#ifndef KEYBOARD_H_
#define KEYBOARD_H_

#define BOARD003

#ifdef BOARD003
  #define MAX_PINS 30  
#else
  #define MAX_PINS 5
#endif

// Debounce is 'settling time' for the keypress, so a noisy key will take longer
#define DEBOUNCE_MS 10

// TODO: Figure out NKRO!
#define KEYBOARD_REPORT_SIZE 6
#define KEYBOARD_SCAN_RATE_US 125

#define SPECIAL_KEY_MOD 0xfe
#define SPECIAL_KEY_BENCHMARK 0xfd
#define NO_PIN -1

bool keyboard_pin_valid(int i);

int keyboard_config_read(uint8_t config[], uint8_t len);
void keyboard_config_set(uint8_t config[], uint8_t len);

void keyboard_init();
bool keyboard_update();

void key_press(int key_code);
void key_release(int key_code);

uint8_t * get_key_report();

#endif /* KEYBOARD_H_ */