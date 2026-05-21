#include <Arduino.h>

// Pro Micro has no LED on pin 13.
// LED_BUILTIN_TX (pin 30) is active-low — drive LOW to turn on.
#define BLINK_LED  LED_BUILTIN_TX
#define BLINK_MS   500

void setup() {
    pinMode(BLINK_LED, OUTPUT);
    digitalWrite(BLINK_LED, HIGH); // off
}

void loop() {
    digitalWrite(BLINK_LED, LOW);  // on
    delay(BLINK_MS);
    digitalWrite(BLINK_LED, HIGH); // off
    delay(BLINK_MS);
}
