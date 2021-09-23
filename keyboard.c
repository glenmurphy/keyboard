#include "keyboard.h"

#include "pico/stdlib.h"
#include <string.h> // for memset

#include "bsp/board.h" // for board_get_millis
#include "tusb.h" // for keyboard keys

int key_codes[MAX_KEYS] = {0};
int key_mod_codes[MAX_KEYS] = {0};
int key_pins[MAX_KEYS] = {0};

bool key_reported_state[MAX_KEYS];
int key_reported_time[MAX_KEYS]; // time of last report
bool key_hardware_state[MAX_KEYS]; // current state of hardware
int key_current_edge[MAX_KEYS]; // 0: nothing, 1 - rising, -1: falling

uint8_t key_report[KEYBOARD_REPORT_SIZE] = {0};

int modifier_key = -1;
int esc_key = -1;

int total_keys = 0;
int add_key(int pin, int key_code, int key_mod_code) {
  if (total_keys >= MAX_KEYS) { 
    return -1;
  }

  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);

  key_codes[total_keys] = key_code;
  key_mod_codes[total_keys] = key_mod_code;
  key_pins[total_keys] = pin;

  total_keys++;
  return total_keys - 1; // return the index of the key we just added
}

void keyboard_init() {
  memset(key_codes, 0, MAX_KEYS);
  memset(key_mod_codes, 0, MAX_KEYS);
  memset(key_pins, 0, MAX_KEYS);
  memset(key_reported_state, false, MAX_KEYS);
  memset(key_reported_time, 0, MAX_KEYS);
  memset(key_hardware_state, false, MAX_KEYS);
  memset(key_current_edge, 0, MAX_KEYS);
  memset(key_report, 0, KEYBOARD_REPORT_SIZE);

  // Codes from hid.h in tinyusb/src/class/hid
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

  esc_key = add_key(16, HID_KEY_ESCAPE, NO_KEY);
  add_key(17, HID_KEY_TAB, NO_KEY);
  add_key(18, HID_KEY_SHIFT_LEFT, NO_KEY);

  modifier_key = add_key(12, NO_KEY, NO_KEY); // special modifier
}

void press(int key_code) {
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

void release(int key_code) {
  if (key_code == NO_KEY) return;

  for (int i = 0; i < KEYBOARD_REPORT_SIZE; i++) {
    if (key_report[i] == key_code) {
      key_report[i] = 0;
      return;
    }
  }
}

void update_pressed() {
  bool modifier = key_reported_state[modifier_key];

  for (int i = 0; i < total_keys; i++) {
    if (i == modifier_key)
      continue;
    
    if (key_current_edge[i] == -1) {
      press(modifier ? key_mod_codes[i] : key_codes[i]);
    } else if (key_current_edge[i] == 1) {
      // Releasing both codes is cheap, and doesn't have side effects if do it when we're not down
      release(key_codes[i]);
      release(key_mod_codes[i]);
    }
  }
}

bool speed_test() {
  static int flood = 0;
  
  // Need to space the releases from the presses so that the operating system 
  // doesn't disregard the inputs (maybe it does its own debouncing)
  const int flood_start = 50;
  bool modifier = key_reported_state[modifier_key];
  
  if (key_reported_state[modifier_key] && key_reported_state[esc_key]) {
    flood = flood_start;
  }

  if (flood > 0) {
    if (flood == flood_start - 1)
      press(HID_KEY_A);
    if (flood == flood_start - 2)
      press(HID_KEY_B);
    if (flood == flood_start - 3)
      press(HID_KEY_C);
    if (flood == flood_start - 4)
      press(HID_KEY_D);
    if (flood == flood_start - 5)
      press(HID_KEY_E);
    if (flood == flood_start - 6)
      press(HID_KEY_F);
    if (flood == 1) {
      release(HID_KEY_A);
      release(HID_KEY_B);
      release(HID_KEY_C);
      release(HID_KEY_D);
      release(HID_KEY_E);
      release(HID_KEY_F);
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
  for (int i = 0; i < total_keys; i++) {
    state = !gpio_get(key_pins[i]);

    if (state == key_hardware_state[i])
      continue;
    
    key_hardware_state[i] = state;
  }

  // For each key, if the hardware state is different than the last reported state
  // and greater than debounce tim has elapsed, report it and log that this frame
  // we have a rising or falling edge
  for (int i = 0; i < total_keys; i++) {
    if (key_hardware_state[i] != key_reported_state[i] && time > key_reported_time[i] + DEBOUNCE_MS) {
      key_reported_state[i] = key_hardware_state[i];
      key_reported_time[i] = time;
      key_current_edge[i] = key_reported_state[i] ? -1 : 1;
      changed = true;
    } else {
      if (key_current_edge[i] != 0)
        changed = true;
      key_current_edge[i] = 0;
    }
  }

  if (changed)
    update_pressed();

  if (speed_test())
    changed = true;

  return changed;
}

bool key_is_rising(int i) { // button released
  return (key_current_edge[i] == 1);
}

bool key_is_falling(int i) { // button pressed (falling to ground)
  return (key_current_edge[i] == -1);
}

uint8_t * get_key_report() {
  return key_report;
};