#include "link_cable.h"

static unsigned long lastClockTime = 0;

void link_init() {
    pinMode(PIN_MOSI, OUTPUT);
    pinMode(PIN_MISO, INPUT);
    pinMode(PIN_SCLK, INPUT);
    digitalWrite(PIN_MOSI, LOW);
    lastClockTime = millis();
}

int link_transferByte(uint8_t sendByte) {
    uint8_t received = 0;
    unsigned long timeout;

    for (int bit = 7; bit >= 0; bit--) {
        // Wait for SCLK to go LOW (falling edge)
        timeout = micros();
        while (READ_GPIO(PIN_SCLK)) {
            if ((micros() - timeout) > CLOCK_TIMEOUT_US) {
                return -1; // Timeout
            }
        }

        // Set MOSI bit while clock is LOW
        if (sendByte & 0x80) {
            WRITE_GPIO_HIGH(PIN_MOSI);
        } else {
            WRITE_GPIO_LOW(PIN_MOSI);
        }
        sendByte <<= 1;

        // Wait for SCLK to go HIGH (rising edge)
        timeout = micros();
        while (!READ_GPIO(PIN_SCLK)) {
            if ((micros() - timeout) > CLOCK_TIMEOUT_US) {
                return -1; // Timeout
            }
        }

        // Read MISO bit on rising edge
        received |= (READ_GPIO(PIN_MISO) << bit);
    }

    lastClockTime = millis();
    return received;
}

bool link_waitForActivity(uint32_t timeout_ms) {
    unsigned long start = millis();
    int lastState = READ_GPIO(PIN_SCLK);

    while ((millis() - start) < timeout_ms) {
        int currentState = READ_GPIO(PIN_SCLK);
        if (currentState != lastState) {
            lastClockTime = millis();
            return true;
        }
    }
    return false;
}

bool link_isIdle(uint32_t idle_ms) {
    // Check for any clock transitions
    static int prevClockState = -1;
    int currentState = READ_GPIO(PIN_SCLK);

    if (prevClockState >= 0 && currentState != prevClockState) {
        lastClockTime = millis();
    }
    prevClockState = currentState;

    return (millis() - lastClockTime) >= idle_ms;
}
