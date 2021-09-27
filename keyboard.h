#include "pico/stdlib.h" // bool, uint8_t

#ifndef KEYBOARD_H_
#define KEYBOARD_H_

#define MAX_PINS 30

#define DEBOUNCE_MS 8
#define KEYBOARD_REPORT_SIZE 6
#define KEYBOARD_POLL_RATE_US 125

#define NO_KEY -1

void keyboard_init();
bool keyboard_update();

void key_press(int key_code);
void key_release(int key_code);

uint8_t * get_key_report();

#endif /* KEYBOARD_H_ */