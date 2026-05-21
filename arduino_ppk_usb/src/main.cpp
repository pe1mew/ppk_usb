/*
 * ppk_usb
 *
 * Copyright (C) 2014 cy384
 * All rights reserved.
 *
 * This software may be modified and distributed under the terms
 * of the BSD license. See the LICENSE file for details.
 */

// Arduino USB HID adapter for the Palm Portable Keyboard

#include <Arduino.h>
#include <Keyboard.h>
#include <SoftwareSerial.h>

// set to 3 for III hardware, or 5 for V hardware
#define PPK_VERSION 5

// set to 1 to enable debug mode, which notes to the arduino console at 9600
#define PPK_DEBUG 0

#if PPK_VERSION == 3
#define VCC_PIN       2
#define RX_PIN        8
#define RTS_PIN       4
#define DCD_PIN       5
#define GND_PIN       6
#endif

#if PPK_VERSION == 5
#define VCC_PIN       7
#define RX_PIN        8
#define RTS_PIN       5
#define DCD_PIN       4
#define GND_PIN       2
#endif

#define PULLDOWN_PIN  15
// set this to any unused pin
#define TX_PIN        11

#if (PPK_VERSION != 3) && (PPK_VERSION != 5)
#error
#error
#error    you did not set your ppk version!
#error    read the instructions or read the code!
#error
#error
#endif

// convenience masks
#define UPDOWN_MASK 0b10000000
#define X_MASK      0b00000111
#define Y_MASK      0b01111000
#define MAP_MASK    0b01111111

// ping keyboard if idle for this many milliseconds (keyboard auto-sleeps after 10 minutes)
#define TIMEOUT 5000

// macro for testing if a char is printable ASCII
#define PRINTABLE_CHAR(x) (((x) >= 32) && ((x) <= 126))

SoftwareSerial keyboard_serial(RX_PIN, TX_PIN, true); // RX, TX, inverted

/**
 * @brief Normal-layer key map indexed by 7-bit scan code (Y[3:0] | X[2:0]).
 *
 * Stored in flash (PROGMEM) to preserve SRAM. Read with pgm_read_byte().
 * Unmapped positions are 0 (no action). Rows Y0-Y11 occupy indices 0-95;
 * indices 96-127 are unused padding.
 */
const uint8_t key_map[128] PROGMEM = {
  /* Y0  */ '1',            '2',             '3',            'z',             '4',  '5',  '6',  '7',
  /* Y1  */ KEY_LEFT_GUI,   'q',             'w',            'e',             'r',  't',  'y',  '`',
  /* Y2  */ 'x',            'a',             's',            'd',             'f',  'g',  'h',  ' ',
  /* Y3  */ KEY_CAPS_LOCK,  KEY_TAB,         KEY_LEFT_CTRL,  0,               0,    0,    0,    0,
  /* Y4  */ 0,              0,               0,              KEY_LEFT_ALT,    0,    0,    0,    0,
  /* Y5  */ 0,              0,               0,              0,               'c',  'v',  'b',  'n',
  /* Y6  */ '-',            '=',             KEY_BACKSPACE,  0,               '8',  '9',  '0',  ' ',
  /* Y7  */ '[',            ']',             '\\',           0,               'u',  'i',  'o',  'p',
  /* Y8  */ '\'',           KEY_RETURN,      0,              0,               'j',  'k',  'l',  ';',
  /* Y9  */ '/',            KEY_UP_ARROW,    0,              0,               'm',  ',',  '.',  0,
  /* Y10 */ KEY_DELETE,     KEY_LEFT_ARROW,  KEY_DOWN_ARROW, KEY_RIGHT_ARROW, 0,    0,    0,    0,
  /* Y11 */ KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, 0,              0,               0,    0,    0,    0,
  /* --- */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /* --- */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

/**
 * @brief Fn-layer key map indexed by 7-bit scan code (Y[3:0] | X[2:0]).
 *
 * Stored in flash (PROGMEM) to preserve SRAM. Read with pgm_read_byte().
 * Active when the Fn key is held: number row -> F1-F12,
 * arrow keys -> Home/End/PgUp/PgDn, Tab -> Escape. All other entries are 0.
 */
const uint8_t fn_key_map[128] PROGMEM = {
  /* Y0  */ KEY_F1,  KEY_F2,      KEY_F3,       0,       KEY_F4, KEY_F5,  KEY_F6,  KEY_F7,
  /* Y1  */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* Y2  */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* Y3  */ 0,       KEY_ESC,     0,            0,       0,      0,       0,       0,
  /* Y4  */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* Y5  */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* Y6  */ KEY_F11, KEY_F12,     0,            0,       KEY_F8, KEY_F9,  KEY_F10, 0,
  /* Y7  */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* Y8  */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* Y9  */ 0,       KEY_PAGE_UP, 0,            0,       0,      0,       0,       0,
  /* Y10 */ 0,       KEY_HOME,    KEY_PAGE_DOWN,KEY_END, 0,      0,       0,       0,
  /* Y11 */ 0,       0,           0,            0,       0,      0,       0,       0,
  /* --- */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  /* --- */ 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
};

char last_byte = 0;
char key_byte = 0;

int fn_key_down = 0;

unsigned long last_comm = 0;
int key_count = 0;

#if PPK_DEBUG
/**
 * @brief Prints a byte value in binary notation to the serial console.
 *
 * Output format: "0b" followed by 8 bits, MSB first.
 * Only active when PPK_DEBUG is non-zero.
 *
 * @param bin_byte The byte value to print.
 */
void print_byte_bin(uint8_t bin_byte) {
  Serial.print("0b");

  for (int i = 7; i > -1; i--) Serial.print(int((bin_byte & (1 << i)) >> i));
}

/**
 * @brief Prints a key press or release event to the serial console.
 *
 * Logs the raw scan byte, the resolved key code (in binary and, if
 * printable, as an ASCII character), and the event direction.
 * Only active when PPK_DEBUG is non-zero.
 *
 * @param key_byte  The 7-bit masked scan code (UPDOWN bit already cleared).
 * @param key_code  The resolved key code from key_map or fn_key_map; 0 if
 *                  the key is unmapped (e.g. Fn itself).
 * @param key_up    Non-zero for a key-release event, zero for key-press.
 */
void print_keychange(uint8_t key_byte, uint8_t key_code, int key_up) {
  if (key_up) Serial.print("released: "); else Serial.print("pressed:  ");
  print_byte_bin(key_byte);

  Serial.print(" mapped to ");

  if (key_code) {
    print_byte_bin(key_code);

    if (PRINTABLE_CHAR(key_code)) {
      Serial.print(" (");
      Serial.print(key_code);
      Serial.print(")");
    } else {
      Serial.print(" (unprintable)");
    }
  } else if (key_byte == 34) {
    // Fn has no keycode, special case it
    Serial.print("Fn");
  } else {
    Serial.print("nothing");
  }

  Serial.println("");
}
#endif // PPK_DEBUG

/**
 * @brief Verifies that the keyboard is still connected by performing an RTS
 *        handshake and checking for the expected ID response.
 *
 * Flushes any pending serial data, toggles RTS low then high, and waits up
 * to 500 ms for the two-byte keyboard ID (0xFA 0xFD).  Any additional bytes
 * received alongside the ID (e.g. keys held at check time) are discarded.
 *
 * This function is used both to prevent the keyboard from entering automatic
 * low-power mode (idle path) and to detect physical disconnection (key-repeat
 * path).
 *
 * @return true   Keyboard responded with the correct ID -- still attached.
 * @return false  No valid response within 500 ms -- keyboard detached or
 *                unresponsive.
 */
bool check_keyboard() {
  while (keyboard_serial.available()) keyboard_serial.read();

  digitalWrite(RTS_PIN, LOW);
  delay(10);
  digitalWrite(RTS_PIN, HIGH);

  unsigned long start = millis();
  while (keyboard_serial.available() < 2) {
    if (millis() - start > 500) {
      return false;
    }
  }

  int b1 = keyboard_serial.read();
  int b2 = keyboard_serial.read();

  // discard any key codes sent with the ID (keys held at check time)
  delay(10);
  while (keyboard_serial.available()) keyboard_serial.read();

  return (b1 == 0xFA) && (b2 == 0xFD);
}

/**
 * @brief Performs the full keyboard power-up and handshake sequence.
 *
 * Powers the keyboard by raising VCC_PIN, waits for the DCD line to assert,
 * completes the RTS handshake (raise if low, toggle if already high), and
 * verifies the two-byte keyboard ID (0xFA 0xFD) on the serial line.  Halts
 * in an infinite loop if the ID is not received.
 *
 * @note This function blocks until the keyboard responds.  On success,
 *       last_comm is set to the current time so the idle timeout starts fresh.
 */
void boot_keyboard() {
  if (PPK_DEBUG) {
    // delay for a bit to allow for opening serial monitor etc.
    for (int i = 0; i < 15; delay(1000 + i++)) Serial.print(".");

    Serial.println("beginning keyboard boot sequence");
  }

  pinMode(VCC_PIN, OUTPUT);
  pinMode(GND_PIN, OUTPUT);
  pinMode(PULLDOWN_PIN, OUTPUT);

  pinMode(RX_PIN, INPUT_PULLUP);
  pinMode(DCD_PIN, INPUT);
  pinMode(RTS_PIN, INPUT);

  digitalWrite(VCC_PIN, LOW);
  digitalWrite(GND_PIN, LOW);
  digitalWrite(PULLDOWN_PIN, LOW);
  digitalWrite(VCC_PIN, HIGH);

  keyboard_serial.begin(9600);
  keyboard_serial.listen();

  if (PPK_DEBUG) Serial.print("waiting for keyboard response...");

  while (digitalRead(DCD_PIN) != HIGH) {;};

  if (PPK_DEBUG) Serial.println(" done");

  if (PPK_DEBUG) Serial.print("finishing handshake...");

  if (digitalRead(RTS_PIN) == LOW) {
    delay(10);
    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, HIGH);
  } else {
    pinMode(RTS_PIN, OUTPUT);
    digitalWrite(RTS_PIN, HIGH);
    digitalWrite(RTS_PIN, LOW);
    delay(10);
    digitalWrite(RTS_PIN, HIGH);
  }

  delay(5);

  if (PPK_DEBUG) Serial.println(" done");

  if (PPK_DEBUG) Serial.print("waiting for keyboard serial ID...");

  while (keyboard_serial.available() < 2) {;};

  if (PPK_DEBUG) Serial.println(" done");

  int byte1 = keyboard_serial.read();
  int byte2 = keyboard_serial.read();

  if (!((byte1 == 0xFA) && (byte2 == 0xFD))) {
    if (PPK_DEBUG) Serial.println("got wrong bytes? giving up here");

    while (1) {;};
  }

  last_comm = millis();
}

/**
 * @brief Arduino setup routine -- runs once on power-up or reset.
 *
 * Initialises the serial console (debug mode only), boots the keyboard,
 * and starts the USB HID keyboard emulation.
 */
void setup() {
  if (PPK_DEBUG) {
    Serial.begin(9600);
    Serial.print("compiled in debug mode with PPK_VERSION ");
    Serial.println(PPK_VERSION);
  }

  boot_keyboard();

  Keyboard.begin();

  if (PPK_DEBUG) Serial.println("setup completed");
}

/**
 * @brief Arduino main loop -- runs continuously after setup().
 *
 * When serial data is available, reads all pending scan bytes and translates
 * each to a USB HID press or release event using the active key map (normal
 * or Fn layer).  A duplicate key-up byte (Last Key Up protocol) causes all
 * keys to be released.
 *
 * After every 10 key events, and when the serial buffer is idle, calls
 * check_keyboard() to detect physical disconnection.
 *
 * When no serial data has been received for TIMEOUT milliseconds, calls
 * check_keyboard() to prevent the keyboard from entering automatic low-power
 * mode.  A full power-cycle is performed only if check_keyboard() fails.
 */
void loop() {
  if (keyboard_serial.available()) {
    while (keyboard_serial.available()) {
      key_byte = keyboard_serial.read();

      bool key_up = key_byte & UPDOWN_MASK;
      uint8_t idx = key_byte & MAP_MASK;
      uint8_t fn_code = pgm_read_byte(&fn_key_map[idx]);
      uint8_t key_code = (fn_key_down && fn_code) ? fn_code : pgm_read_byte(&key_map[idx]);

      // keyboard duplicates the final key-up byte
      if (key_byte == last_byte) {
        Keyboard.releaseAll();
      } else {
#if PPK_DEBUG
        print_keychange(key_byte & MAP_MASK, key_code, key_up);
#endif

        if (key_code != 0) {
          if (key_up) {
            Keyboard.release(key_code);
          } else {
            Keyboard.press(key_code);
          }
        } else {
          // special case the Fn key
          if (idx == 34) {
            fn_key_down = !key_up;
          }
        }
      }

      last_byte = key_byte;
      last_comm = millis();
      key_count++;
    }
    if (key_count >= 10 && !keyboard_serial.available()) {
      key_count = 0;
      if (!check_keyboard()) {
        if (PPK_DEBUG) Serial.println("keyboard detached");
        Keyboard.releaseAll();
        digitalWrite(VCC_PIN, LOW);
        boot_keyboard();
      } else {
        last_comm = millis();
      }
    }
  } else {
    // ping keyboard to prevent auto-sleep and detect detachment
    if ((millis() - last_comm) > TIMEOUT) {
      if (PPK_DEBUG) Serial.println("keyboard idle, checking connection");
      if (!check_keyboard()) {
        if (PPK_DEBUG) Serial.println("keyboard not responding, power cycling");
        Keyboard.releaseAll();
        digitalWrite(VCC_PIN, LOW);
        boot_keyboard();
      } else {
        last_comm = millis();
      }
    }
  }
}
