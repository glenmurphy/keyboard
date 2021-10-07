#include "bsp/board.h"
#include "led.h"

int blink_interval_ms = LED_BLINK_NOT_MOUNTED;
bool led_state = false;

void led_init() {
  blink_interval_ms = LED_BLINK_DEFAULT;
}

void led_blink(int interval) {
  blink_interval_ms = interval;
}

void led_solid(bool on) {
  blink_interval_ms = LED_BLINK_DISABLED;
  board_led_write(on);
  led_state = on;
}

void led_task() {
  static uint32_t start_ms = 0;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}