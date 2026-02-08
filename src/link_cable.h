#ifndef LINK_CABLE_H
#define LINK_CABLE_H

#include "config.h"

// Initialize link cable GPIO pins
void link_init();

// Exchange one byte with the Game Boy (blocking).
// Sends `sendByte` while simultaneously receiving a byte.
// Returns the received byte, or -1 on timeout.
int link_transferByte(uint8_t sendByte);

// Wait for any clock activity within timeout_ms.
// Returns true if clock edge detected, false on timeout.
bool link_waitForActivity(uint32_t timeout_ms);

// Check if the clock has been idle for at least idle_ms milliseconds.
// Non-blocking: returns true if idle, false if clock is still active.
bool link_isIdle(uint32_t idle_ms);

#endif // LINK_CABLE_H
