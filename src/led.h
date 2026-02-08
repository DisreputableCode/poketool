#ifndef LED_H
#define LED_H

#include "config.h"

enum LedPattern {
    LED_OFF,
    LED_SOLID,
    LED_SLOW_BLINK,     // Idle: 1s on, 1s off
    LED_FAST_BLINK,     // Exchanging data: 100ms cycle
    LED_DOUBLE_BLINK,   // Trade connected
    LED_TRIPLE_BLINK,   // Clone mode active
    LED_VERY_FAST_BLINK // Error: 50ms cycle
};

void led_init();
void led_setPattern(LedPattern pattern);
void led_update();  // Call in loop()

#endif // LED_H
