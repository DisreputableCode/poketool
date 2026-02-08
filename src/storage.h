#ifndef STORAGE_H
#define STORAGE_H

#include "config.h"

// =============================================================================
// Stored Pokemon Slot
// =============================================================================

struct StoredPokemon {
    uint8_t monData[GEN2_PARTY_STRUCT_SIZE]; // 48 bytes max (Gen1 uses 44)
    uint8_t ot[NAME_LENGTH];                 // 11 bytes
    uint8_t nickname[NAME_LENGTH];           // 11 bytes
    uint8_t speciesIndex;                    // 1 byte
    bool occupied;
};

// =============================================================================
// Storage API
// =============================================================================

// Load all slots from NVS into RAM cache
void storage_init();

// Save a Pokemon to a slot (writes to NVS + RAM cache)
void storage_saveSlot(Generation gen, int slot, const StoredPokemon* mon);

// Clear a slot (removes from NVS + RAM cache)
void storage_clearSlot(Generation gen, int slot);

// Count occupied slots for a generation
int storage_getCount(Generation gen);

// Get the RAM-cached slot array (6 slots) for a generation
StoredPokemon* storage_getParty(Generation gen);

// Persist and retrieve trade mode
void storage_setTradeMode(TradeMode mode);
TradeMode storage_getTradeMode();

#endif // STORAGE_H
