/**
 * Keyboard handler for the Raspberry Pico; this is a simple implementation and
 * doesn't bother with key matrix scanning because we have enough GPIO pins on 
 * the Pico for what we need (a gaming keyboard)
 */
#include "keyboard.h"

#include "pico/stdlib.h"
#include <string.h> // for memset
#include <stdlib.h> // malloc

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
uint8_t key_report[KEYBOARD_REPORT_SIZE];

#ifdef BOARD003
  bool keyboard_pin_valid(int pin) {
    return (pin >= 0 && pin < MAX_PINS);
  }
#else
  bool keyboard_pin_valid(int i) {
    return (i == 0 || i == 5);
  }
#endif

int modifier_pin = NO_PIN;

void set_key(int pin, int key_code, int keycode_alt) {
  if (keyboard_pin_valid(pin) == false) {
    return;
  }
  if (key_code == HID_KEY_NONE && keycode_alt == HID_KEY_NONE) {
    pins[pin].watched = false;
    if (modifier_pin == pin)
      modifier_pin = NO_PIN;

    return;
  }

  gpio_init(pin);
  gpio_set_dir(pin, GPIO_IN);
  gpio_pull_up(pin);

  if (key_code == SPECIAL_KEY_MOD) {
    modifier_pin = pin;
  }

  pins[pin].watched = true;
  pins[pin].state = false;
  pins[pin].reported_state = false;
  pins[pin].reported_time = 0;
  pins[pin].current_edge = 0;
  pins[pin].keycode = key_code;
  pins[pin].keycode_alt = keycode_alt;
}

void keyboard_set_default() {
#ifdef BOARD003
  // Codes from tinyusb/src/class/hid/hid.h
  // https://github.com/hathach/tinyusb/blob/master/src/class/hid/hid.h
  set_key(0, HID_KEY_ESCAPE, SPECIAL_KEY_BENCHMARK);
  set_key(1, HID_KEY_TAB, HID_KEY_NONE);
  set_key(2, HID_KEY_SHIFT_LEFT, HID_KEY_NONE);

  //set_key(3, HID_KEY_NONE, HID_KEY_NONE);
  set_key(4, HID_KEY_Q, HID_KEY_1);
  set_key(5, HID_KEY_A, HID_KEY_F1);
  set_key(6, HID_KEY_Z, HID_KEY_F4);

  //set_key(7, HID_KEY_NONE, HID_KEY_NONE);
  set_key(8, HID_KEY_W, HID_KEY_2);
  set_key(9, HID_KEY_S, HID_KEY_F2);
  set_key(10, HID_KEY_X, HID_KEY_F5);

  set_key(11, SPECIAL_KEY_MOD, HID_KEY_NONE); // special modifier

  //set_key(12, HID_KEY_NONE, HID_KEY_NONE);
  set_key(13, HID_KEY_E, HID_KEY_3);
  set_key(14, HID_KEY_D, HID_KEY_F3);
  set_key(15, HID_KEY_C, HID_KEY_F6);

  set_key(16, HID_KEY_CONTROL_LEFT, HID_KEY_VOLUME_DOWN);
  set_key(18, HID_KEY_ALT_LEFT, HID_KEY_VOLUME_UP);
  set_key(17, HID_KEY_SPACE, HID_KEY_PERIOD);

  set_key(19, HID_KEY_V, HID_KEY_SLASH);
  set_key(20, HID_KEY_F, HID_KEY_ENTER);
  set_key(21, HID_KEY_R, HID_KEY_4);
#endif
}

void keyboard_init() {
  for (int i = 0; i < MAX_PINS; i++) {
    pins[i].watched = false;
    pins[i].keycode = 0;
    pins[i].keycode_alt = 0;
  }

  keyboard_set_default();

  // This is more for validation/testing than anything else - will remove eventually
  uint8_t config[128];
  uint8_t len = keyboard_config_read(config, sizeof(config));
  keyboard_config_set(config, len);
}

int keyboard_config_read(uint8_t config[], uint8_t len) {
  memset(config, 0, len);

  int index = 0;
  for (int i = 0; i < MAX_PINS && index < len; i++) {
    if (!pins[i].watched)
      continue;

    config[index + 0] = i;
    config[index + 1] = pins[i].keycode;
    config[index + 2] = pins[i].keycode_alt;
    index += 3;
  }

  return index;
}

void keyboard_config_set(uint8_t config[], uint8_t len) {
  for (int index = 0; index < len; index += 3) {
    int pin = config[index];
    int key_code = config[index + 1];
    int keycode_alt = config[index + 2];

    set_key(pin, key_code, keycode_alt);
  }
}

void key_press(int key_code) {
  if (key_code == HID_KEY_NONE) return;

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
  if (key_code == HID_KEY_NONE) return;

  for (int i = 0; i < KEYBOARD_REPORT_SIZE; i++) {
    if (key_report[i] == key_code) {
      key_report[i] = 0;
      return;
    }
  }
}

bool modifier_state() {
  if (modifier_pin == NO_PIN)
    return false;

  return pins[modifier_pin].reported_state;
}

void keyboard_update_pressed() {
  bool modifier = modifier_state();

  for (int i = 0; i < MAX_PINS; i++) {
    if (!pins[i].watched) continue;

    if (pins[i].keycode == SPECIAL_KEY_MOD)
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
  if (pins[modifier_pin].reported_state && pins[0].reported_state) {
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
      // If the pin is in a different state to what was reported, and we're after the debounce time,
      // change the state of the switch
      pins[i].reported_state = pins[i].state;
      pins[i].reported_time = time;
      pins[i].current_edge = pins[i].reported_state ? -1 : 1;
      changed = true;
    } else if (pins[i].state != pins[i].reported_state && time > pins[i].reported_time + DEBOUNCE_MS) {
      // Otherwise if the pin has changed and we're in the debounce time, extend the debounce time
      pins[i].reported_time = time;
    } else if (pins[i].current_edge != 0) {
      // Otherwise if the pin hasn't changed, but did change last frame, reset the edge
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