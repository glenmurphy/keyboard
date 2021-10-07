#ifndef LED_H_
#define LED_H_

enum  {
  LED_BLINK_DEFAULT = 50,
  LED_BLINK_NOT_MOUNTED = 100,
  LED_BLINK_MOUNTED = 500,
  LED_BLINK_SUSPENDED = 2500,
  LED_BLINK_DISABLED = 0,
};

void led_init();
void led_blink(int interval);
void led_solid(bool on);
void led_task();

#endif /* LED_H_ */