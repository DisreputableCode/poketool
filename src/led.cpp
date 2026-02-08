#include "led.h"

static LedPattern currentPattern = LED_OFF;
static unsigned long patternStart = 0;
static bool ledState = false;

void led_init() {
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, LOW);
}

void led_setPattern(LedPattern pattern) {
    if (pattern != currentPattern) {
        currentPattern = pattern;
        patternStart = millis();
        ledState = false;
        digitalWrite(PIN_LED, LOW);
    }
}

void led_update() {
    unsigned long now = millis();
    unsigned long elapsed = now - patternStart;

    switch (currentPattern) {
        case LED_OFF:
            if (ledState) {
                ledState = false;
                digitalWrite(PIN_LED, LOW);
            }
            break;

        case LED_SOLID:
            if (!ledState) {
                ledState = true;
                digitalWrite(PIN_LED, HIGH);
            }
            break;

        case LED_SLOW_BLINK: {
            bool on = (elapsed / 1000) % 2 == 0;
            if (on != ledState) {
                ledState = on;
                digitalWrite(PIN_LED, ledState);
            }
            break;
        }

        case LED_FAST_BLINK: {
            bool on = (elapsed / 100) % 2 == 0;
            if (on != ledState) {
                ledState = on;
                digitalWrite(PIN_LED, ledState);
            }
            break;
        }

        case LED_DOUBLE_BLINK: {
            // Two quick flashes every 2 seconds
            unsigned long pos = elapsed % 2000;
            bool on = (pos < 100) || (pos >= 200 && pos < 300);
            if (on != ledState) {
                ledState = on;
                digitalWrite(PIN_LED, ledState);
            }
            break;
        }

        case LED_TRIPLE_BLINK: {
            // Three quick flashes every 2 seconds
            unsigned long pos = elapsed % 2000;
            bool on = (pos < 100) || (pos >= 200 && pos < 300) || (pos >= 400 && pos < 500);
            if (on != ledState) {
                ledState = on;
                digitalWrite(PIN_LED, ledState);
            }
            break;
        }

        case LED_VERY_FAST_BLINK: {
            bool on = (elapsed / 50) % 2 == 0;
            if (on != ledState) {
                ledState = on;
                digitalWrite(PIN_LED, ledState);
            }
            break;
        }
    }
}
