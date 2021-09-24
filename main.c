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
 *
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"
#include "usb_descriptors.h"

#include "keyboard.h"

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum  {
  BLINK_NOT_MOUNTED = 50,
  BLINK_MOUNTED = 500,
  BLINK_SUSPENDED = 2500,
  BLINK_ALWAYS_ON = 5000,
};

static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;

// CDC stuff
#define URL  "example.tinyusb.org/webusb-serial/"
const tusb_desc_webusb_url_t desc_url =
{
  .bLength         = 3 + sizeof(URL) - 1,
  .bDescriptorType = 3, // WEBUSB URL type
  .bScheme         = 1, // 0: http, 1: https
  .url             = URL
};
static bool web_serial_connected = false;

//------------- prototypes -------------//
void led_blinking_task(void);
void cdc_task(void);
void webserial_task(void);
void hid_task(void);

/*------------- MAIN -------------*/
int main(void)
{
  board_init();
  tusb_init();
  keyboard_init();
    
  while (1)
  {
    tud_task(); // tinyusb device task
    
    hid_task();
    webserial_task();
    
    led_blinking_task();
  }

  return 0;
}

//--------------------------------------------------------------------+
// Device callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
  blink_interval_ms = BLINK_NOT_MOUNTED;
}

// Invoked when usb bus is suspended
// remote_wakeup_en : if host allow us to perform remote wakeup
// Within 7ms, device must draw an average of current less than 2.5 mA from bus
void tud_suspend_cb(bool remote_wakeup_en)
{
  (void) remote_wakeup_en;
  blink_interval_ms = BLINK_SUSPENDED;
}

// Invoked when usb bus is resumed
void tud_resume_cb(void)
{
  blink_interval_ms = BLINK_MOUNTED;
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

  // use to avoid send multiple consecutive zero report for keyboard
  tud_hid_keyboard_report(REPORT_ID_KEYBOARD, 0, get_key_report());
  hid_queued = false;
}

// tud_hid_report_complete_cb() is used to send the next report after previous one is complete
void hid_task(void)
{
  // Poll very quickly - faster than our USB polling rate
  // (see TUD_HID_DESCRIPTOR in usb_descriptors.c)
  const uint64_t interval_us = KEYBOARD_POLL_RATE_US - 1;
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
        blink_interval_ms = 0;
        board_led_write(true);
      } else {
        // Caplocks Off: back to normal blink
        board_led_write(false);
        blink_interval_ms = BLINK_MOUNTED;
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
  // echo to web serial
  if ( web_serial_connected )
  {
    tud_vendor_write(buf, count);
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
          board_led_write(true);
          blink_interval_ms = 100;

          tud_vendor_write_str("\r\nTinyUSB WebUSB device example\r\n");
        } else {
          blink_interval_ms = 50;
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
  //if (!web_serial_connected)
  //  return;

  if (!tud_vendor_available())
    return;

  uint8_t buf[64];
  uint32_t count = tud_vendor_read(buf, sizeof(buf));
  printf("read: %d", count);
  if (count > 0) {
    // echo back to both web serial and cdc
    echo_all(buf, count);
  }
}

//--------------------------------------------------------------------+
// BLINKING TASK
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  // blink is disabled
  if (!blink_interval_ms) return;

  // Blink every interval ms
  if ( board_millis() - start_ms < blink_interval_ms) return; // not enough time
  start_ms += blink_interval_ms;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}
