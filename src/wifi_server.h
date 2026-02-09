#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include "config.h"
#include <stdarg.h>

// Forward declarations for enums used by TradeContext
// (ConnectionState and TradeCentreState are defined in main.cpp)

// =============================================================================
// Trade Context — shared state between main loop and web server
// =============================================================================

struct TradeContext {
    // State (written by main loop, read by web server)
    volatile int connState;             // ConnectionState enum value
    volatile int tcState;               // TradeCentreState enum value
    volatile int gen;                   // Generation enum value
    volatile int tradePokemon;          // GB's selection (-1 = none)

    // Opponent party info (written after party exchange, read by web)
    volatile int opponentCount;
    volatile uint8_t opponentSpecies[PARTY_LENGTH];
    volatile uint8_t opponentLevels[PARTY_LENGTH];
    volatile uint8_t opponentNicknames[PARTY_LENGTH][NAME_LENGTH];

    // Web UI control (written by web server, read by main loop)
    volatile int offerSlot;             // Which of our slots to offer (default 0)
    volatile bool autoConfirm;          // Auto-confirm trades? (default true)
    volatile bool confirmRequested;     // Web UI clicked confirm
    volatile bool declineRequested;     // Web UI clicked decline

    // Mode
    volatile int tradeMode;             // TradeMode enum value
};

// Start WiFi AP and web server. Must be called after storage_init().
void wifi_init(TradeContext* ctx);

// =============================================================================
// Debug logging — streams to SSE /events endpoint
// =============================================================================

// Printf-style log: writes to Serial AND sends to SSE "log" event
void debug_logf(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Record an SPI byte exchange (batched, low overhead in hot path)
void debug_spi(uint8_t sent, uint8_t recv);

// Flush any pending SPI data to SSE (call during idle)
void debug_spi_flush();

#endif // WIFI_SERVER_H
