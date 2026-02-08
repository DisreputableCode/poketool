#ifndef WIFI_SERVER_H
#define WIFI_SERVER_H

#include "config.h"

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

#endif // WIFI_SERVER_H
