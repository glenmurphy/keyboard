#include "pico/stdlib.h"

#ifndef KEYBOARD_H_
#define KEYBOARD_H_

#define MAX_KEYS 26
#define DEBOUNCE_MS 8
#define KEYBOARD_REPORT_SIZE 6
#define KEYBOARD_POLL_RATE_US 125

#define NO_KEY -1

void keyboard_init();
bool keyboard_update();

bool key_is_rising(int i); 
bool key_is_falling(int i);

void press(int key_code);
void release(int key_code);

uint8_t * get_key_report();

#endif /* KEYBOARD_H_ */