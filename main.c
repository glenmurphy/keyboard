/* 
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "keyboard.h"
#include "led.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+
// WebUSB stuff
#define URL "example.tinyusb.org/webusb-serial/"
const tusb_desc_webusb_url_t desc_url =
{
  .bLength         = 3 + sizeof(URL) - 1,
  .bDescriptorType = 3, // WEBUSB URL type
  .bScheme         = 1, // 0: http, 1: https
  .url             = URL
};
static bool web_serial_connected = false;

//------------- prototypes -------------//
void webserial_task(void);
void hid_task(void);

/*------------- MAIN -------------*/
int main(void)
{
  board_init();
  tusb_init();

  //set_sys_clock_khz(200000, true);
  keyboard_init();
  led_init();
    
  while (1)
  {
    tud_task(); // tinyusb device task
    
    hid_task();
    webserial_task();
    
    led_update();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  led_solid(true);
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  led_blink(LED_BLINK_NOT_MOUNTED);
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  led_blink(LED_BLINK_SUSPENDED);
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  led_solid(true);
}

//--------------------------------------------------------------------+
// HID
//--------------------------------------------------------------------+
bool hid_queued = false;
static void send_hid_report()
{
  if (!tud_hid_ready()) {
    hid_queued = true;
    return;
  }

  uint8_t * report = get_key_report();
  tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, report);

  hid_queued = false;
}

static void send_media_report()
{
  static uint16_t media_key_held = 0;
  uint16_t media_key = 0;
  uint8_t * report = get_key_report();

  for (int i = 0; i < KEYBOARD_REPORT_SIZE; i++) {
    if (report[i] == HID_KEY_VOLUME_UP)
      media_key = HID_USAGE_CONSUMER_VOLUME_INCREMENT;
    else if (report[i] == HID_KEY_VOLUME_DOWN)
      media_key = HID_USAGE_CONSUMER_VOLUME_DECREMENT; 
    else if (report[i] == HID_KEY_MUTE)
      media_key = HID_USAGE_CONSUMER_MUTE;
  }
  
  if (media_key != 0 && media_key != media_key_held) {
    board_delay(2); // space from previous report .. because
    media_key_held = media_key;
    tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &media_key, 2);
  } else if (media_key == 0 && media_key_held != 0){
    board_delay(2); // space from previous report .. because
    media_key_held = 0;
    uint16_t empty_key = 0;
    tud_hid_report(REPORT_ID_CONSUMER_CONTROL, &empty_key, 2);
  }
}

void send_webusb_report() {
  if (!web_serial_connected)
    return;
  
  uint8_t message[7];
  message[0] = 'r';
  uint8_t * report = get_key_report(); // need to replace this with a pins-down map
  for (int i = 0; i < KEYBOARD_REPORT_SIZE; i++) {
    message[i + 1] = report[i];
  }
  tud_vendor_write(message, 7);
}

// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll very quickly - faster than our USB polling rate so we always have fresh data
  // available (see TUD_HID_DESCRIPTOR in usb_descriptors.c)
  const uint64_t interval_us = KEYBOARD_SCAN_RATE_US;
  static uint64_t start_us = 0;

  if (time_us_64() - start_us < interval_us) return; // not enough time
  start_us += interval_us;

  bool changed = keyboard_update();
  if (!hid_queued && !changed) return;

  // Remote wakeup
  if (tud_suspended()) {
    // Wake up host if we are in suspend mode
    // and REMOTE_WAKEUP feature is enabled by host
    tud_remote_wakeup();
  } else {
    // Send the 1st of report chain, the rest will be sent by tud_hid_report_complete_cb()
    send_hid_report();
    send_media_report();
    send_webusb_report();
  }
}

// Invoked when sent REPORT successfully to host
// Application can use this to send the next report
// Note: For composite reports, report[0] is report ID
void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint8_t len)
{
  (void) instance;
  (void) len;

  return;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize)
{
  (void) instance;

  if (report_type == HID_REPORT_TYPE_OUTPUT)
  {
    // Set keyboard LED e.g Capslock, Numlock etc...
    if (report_id == REPORT_ID_KEYBOARD)
    {
      // bufsize should be (at least) 1
      if ( bufsize < 1 ) return;

      uint8_t const kbd_leds = buffer[0];

      if (kbd_leds & KEYBOARD_LED_CAPSLOCK) {
        // Capslock On: disable blink, turn led on
        led_solid(true);
      } else {
        // Caplocks Off: back to normal blink
        led_blink(LED_BLINK_MOUNTED);
      }
    }
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void) instance;
  (void) report_id;
  (void) report_type;
  (void) buffer;
  (void) reqlen;

  return 0;
}

// send characters to both CDC and WebUSB
void echo_all(uint8_t buf[], uint32_t count)
{
  if ( web_serial_connected ) {
    
  }
}

//--------------------------------------------------------------------+
// WebUSB use vendor class
//--------------------------------------------------------------------+

// Invoked when a control transfer occurred on an interface of this class
// Driver response accordingly to the request and the transfer stage (setup/data/ack)
// return false to stall control endpoint (e.g unsupported request)
bool tud_vendor_control_xfer_cb(uint8_t rhport, uint8_t stage, tusb_control_request_t const * request)
{
  // nothing to with DATA & ACK stage
  if (stage != CONTROL_STAGE_SETUP) return true;

  switch (request->bmRequestType_bit.type)
  {
    case TUSB_REQ_TYPE_VENDOR:
      switch (request->bRequest)
      {
        case VENDOR_REQUEST_WEBUSB:
          // match vendor request in BOS descriptor
          // Get landing page url
          return tud_control_xfer(rhport, request, (void*) &desc_url, desc_url.bLength);

        case VENDOR_REQUEST_MICROSOFT:
          if ( request->wIndex == 7 )
          {
            // Get Microsoft OS 2.0 compatible descriptor
            uint16_t total_len;
            memcpy(&total_len, desc_ms_os_20+8, 2);

            return tud_control_xfer(rhport, request, (void*) desc_ms_os_20, total_len);
          } else {
            return false;
          }

        default: break;
      }
    break;

    case TUSB_REQ_TYPE_CLASS:
      if (request->bRequest == 0x22)
      {
        // Webserial simulate the CDC_REQUEST_SET_CONTROL_LINE_STATE (0x22) to connect and disconnect.
        web_serial_connected = (request->wValue != 0);

        // Always lit LED if connected
        if ( web_serial_connected ) {
          led_solid(true);
          tud_vendor_write_str("\r\nTinyUSB WebUSB device example\r\n");
        } else {
          led_blink(LED_BLINK_MOUNTED);
        }

        // response with status OK
        return tud_control_status(rhport, request);
      }
    break;

    default: break;
  }
  // stall unknown request
  return false;
}

void webserial_task(void)
{
  if (!web_serial_connected)
    return;

  if (!tud_vendor_available())
    return;

  uint8_t buf[128]; // need to check this
  uint32_t count = tud_vendor_read(buf, sizeof(buf));
  printf("read: %d", count);

  if (count == 0)
    return;

  if(buf[0] == 'c') {
    // Read the config and send

    uint8_t message[64];
    message[0] = 'c';
    uint8_t size = keyboard_config_read(message + 1, sizeof(message) - 1);
    tud_vendor_write(message, size + 1);

  } else if (buf[0] == 's') {
    // Set the keymap
    keyboard_config_set(buf + 1, count - 1);
  }

  // echo_all(config, MAX_PINS * 3);
  // Need to split this across multiple packets, as the packets are limited to 64 bytes
  /*
  switch(buf[0]) {
    case 'c':
      break;
    default:
      echo_all(buf, count);
  }
  */ 
}