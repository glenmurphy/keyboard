/**
 * Keyboard handler for the Raspberry Pico; this is a simple implementation and
 * doesn't bother with key matrix scanning because we have enough GPIO pins on 
 * the Pico for what we need (a gaming keyboard)
 */
#include "keyboard.h"

#include "pico/stdlib.h"
#include <string.h> // for memset

#include "bsp/board.h" // for board_get_millis
#include "tusb.h" // for keyboard keys

typedef struct {
  bool watched;
  
  bool state;
  bool reported_state;
  int reported_time;
  int current_edge; // 0: nothing, 1 - rising, -1: falling

  int keycode;
  int keycode_alt;
} Pin;

Pin pins[MAX_PINS];
uint8_t key_report[KEYBOARD_REPORT_SIZE] = {0};

#define MODIFIER_PIN 12
#define ESC_PIN 16

int add_key(int pin, int key_code, int keycode_alt) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);

  pins[pin].watched = true;
  pins[pin].state = false;
  pins[pin].reported_state = false;
  pins[pin].reported_time = 0;
  pins[pin].current_edge = 0;
  pins[pin].keycode = key_code;
  pins[pin].keycode_alt = keycode_alt;
}

void keyboard_init() {
  for (int i = 0; i < MAX_PINS; i++) {
    pins[i].watched = false;
  }

  // Codes from tinyusb/src/class/hid/hid.h
  add_key(0, HID_KEY_R, HID_KEY_4);
  add_key(1, HID_KEY_F, HID_KEY_ENTER);
  add_key(2, HID_KEY_V, HID_KEY_BACKSPACE);

  add_key(3, HID_KEY_ALT_RIGHT, NO_KEY);
  add_key(4, HID_KEY_PAGE_UP, NO_KEY);
  add_key(5, HID_KEY_E, HID_KEY_3);
  add_key(6, HID_KEY_D, HID_KEY_F3);
  add_key(7, HID_KEY_C, HID_KEY_F6);
  add_key(8, HID_KEY_SPACE, NO_KEY);
  add_key(9, HID_KEY_W, HID_KEY_2);

  add_key(10, HID_KEY_S, HID_KEY_F2);
  add_key(11, HID_KEY_X, HID_KEY_F5);

  add_key(13, HID_KEY_Q, HID_KEY_1);
  add_key(14, HID_KEY_A, HID_KEY_F1);
  add_key(15, HID_KEY_Z, HID_KEY_F4);

  add_key(ESC_PIN, HID_KEY_ESCAPE, NO_KEY);
  add_key(17, HID_KEY_TAB, NO_KEY);
  add_key(18, HID_KEY_SHIFT_LEFT, NO_KEY);

  add_key(MODIFIER_PIN, NO_KEY, NO_KEY); // special modifier
}

void key_press(int key_code) {
  if (key_code == NO_KEY) return;

  int index = -1;
  for (int i = 0; i < KEYBOARD_REPORT_SIZE; i++) {
    // Check to see if key is already pressed
    if (key_report[i] == key_code)
      return;

    // Found a free slot - we assume it's OK if this was emptied in the same
    // update because replacing a key in a slot will release it anyway
    if (key_report[i] == 0 && index == -1)
      index = i;
  }

  if (index != -1)
    key_report[index] = key_code;
}

void key_release(int key_code) {
  if (key_code == NO_KEY) return;

  for (int i = 0; i < KEYBOARD_REPORT_SIZE; i++) {
    if (key_report[i] == key_code) {
      key_report[i] = 0;
      return;
    }
  }
}

void keyboard_update_pressed() {
  bool modifier = pins[MODIFIER_PIN].reported_state;

  for (int i = 0; i < MAX_PINS; i++) {
    if (!pins[i].watched) continue;

    if (i == MODIFIER_PIN)
      continue;
    
    if (pins[i].current_edge == -1) {
      key_press(modifier ? pins[i].keycode_alt : pins[i].keycode);
    } else if (pins[i].current_edge == 1) {
      // Releasing both codes is cheap, and doesn't have side effects if do it when we're not down
      key_release(pins[i].keycode);
      key_release(pins[i].keycode_alt);
    }
  }
}

bool keyboard_speed_test() {
  static int flood = 0;

  // Need to space the releases from the presses so that the operating system 
  // doesn't disregard the inputs (maybe it does its own debouncing)
  const int flood_start = 50;
  if (pins[MODIFIER_PIN].reported_state && pins[ESC_PIN].reported_state) {
    flood = flood_start;
  }

  if (flood > 0) {
    if (flood == flood_start - 1)
      key_press(HID_KEY_A);
    if (flood == flood_start - 2)
      key_press(HID_KEY_B);
    if (flood == flood_start - 3)
      key_press(HID_KEY_C);
    if (flood == flood_start - 4)
      key_press(HID_KEY_D);
    if (flood == flood_start - 5)
      key_press(HID_KEY_E);
    if (flood == flood_start - 6)
      key_press(HID_KEY_F);
    if (flood == 1) {
      key_release(HID_KEY_A);
      key_release(HID_KEY_B);
      key_release(HID_KEY_C);
      key_release(HID_KEY_D);
      key_release(HID_KEY_E);
      key_release(HID_KEY_F);
    } 
    flood--;
    return true; // test was run, report has changed
  }
  
  return false; // test was not run
}

bool keyboard_update() {
  int time = board_millis();
  bool state = false;
  bool changed = false;

  // Get and store the physical state of the hardware
  for (int i = 0; i < MAX_PINS; i++) {
    if (!pins[i].watched) continue;

    state = !gpio_get(i);

    if (state == pins[i].state) 
      continue; // no change
    
    pins[i].state = state;
  }

  // For each key, if the hardware state is different than the last reported state
  // and greater than debounce time has elapsed, report it and log that this frame
  // we have a rising or falling edge
  for (int i = 0; i < MAX_PINS; i++) {
    if (!pins[i].watched) continue;

    if (pins[i].state != pins[i].reported_state && time > pins[i].reported_time + DEBOUNCE_MS) {
      pins[i].reported_state = pins[i].state;
      pins[i].reported_time = time;
      pins[i].current_edge = pins[i].reported_state ? -1 : 1;
      changed = true;
    } else {
      if (pins[i].current_edge != 0)
        changed = true;
      pins[i].current_edge = 0;
    }
  }

  if (changed)
    keyboard_update_pressed();

  if (keyboard_speed_test())
    changed = true;

  return changed;
}

uint8_t * get_key_report() {
  return key_report;
};