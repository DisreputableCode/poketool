#include "storage.h"
#include <Preferences.h>
#include <string.h>

// =============================================================================
// NVS Storage Implementation
// =============================================================================

static Preferences prefs;
static StoredPokemon gen1Party[PARTY_LENGTH];
static StoredPokemon gen2Party[PARTY_LENGTH];

// NVS key builders — keys like "g1_m0", "g1_o0", "g1_n0", "g1_s0"
static void slotKey(char* buf, const char* prefix, int slot) {
    // e.g. prefix="g1_m", slot=3 -> "g1_m3"
    int len = strlen(prefix);
    memcpy(buf, prefix, len);
    buf[len] = '0' + slot;
    buf[len + 1] = '\0';
}

static void loadSlot(const char* genPrefix, int slot, StoredPokemon* out) {
    char key[8];
    int monSize = (genPrefix[1] == '1') ? GEN1_PARTY_STRUCT_SIZE : GEN2_PARTY_STRUCT_SIZE;

    // Mon data
    slotKey(key, genPrefix, slot);
    key[3] = 'm'; // e.g. "g1_m0"
    size_t got = prefs.getBytes(key, out->monData, monSize);
    if (got != (size_t)monSize) {
        memset(out, 0, sizeof(StoredPokemon));
        out->occupied = false;
        return;
    }

    // OT name
    key[3] = 'o';
    prefs.getBytes(key, out->ot, NAME_LENGTH);

    // Nickname
    key[3] = 'n';
    prefs.getBytes(key, out->nickname, NAME_LENGTH);

    // Species index
    key[3] = 's';
    out->speciesIndex = prefs.getUChar(key, 0);

    out->occupied = true;
}

static void saveSlotNVS(const char* genPrefix, int slot, const StoredPokemon* mon) {
    char key[8];
    int monSize = (genPrefix[1] == '1') ? GEN1_PARTY_STRUCT_SIZE : GEN2_PARTY_STRUCT_SIZE;

    slotKey(key, genPrefix, slot);
    key[3] = 'm';
    prefs.putBytes(key, mon->monData, monSize);

    key[3] = 'o';
    prefs.putBytes(key, mon->ot, NAME_LENGTH);

    key[3] = 'n';
    prefs.putBytes(key, mon->nickname, NAME_LENGTH);

    key[3] = 's';
    prefs.putUChar(key, mon->speciesIndex);
}

static void clearSlotNVS(const char* genPrefix, int slot) {
    char key[8];
    slotKey(key, genPrefix, slot);
    key[3] = 'm'; prefs.remove(key);
    key[3] = 'o'; prefs.remove(key);
    key[3] = 'n'; prefs.remove(key);
    key[3] = 's'; prefs.remove(key);
}

// =============================================================================
// Public API
// =============================================================================

void storage_init() {
    prefs.begin("poketool", false);

    memset(gen1Party, 0, sizeof(gen1Party));
    memset(gen2Party, 0, sizeof(gen2Party));

    for (int i = 0; i < PARTY_LENGTH; i++) {
        loadSlot("g1_x", i, &gen1Party[i]);
        loadSlot("g2_x", i, &gen2Party[i]);
    }

    int g1 = storage_getCount(GEN_1);
    int g2 = storage_getCount(GEN_2);
    Serial.printf("[STORAGE] Loaded %d Gen1, %d Gen2 Pokemon from NVS\n", g1, g2);
}

void storage_saveSlot(Generation gen, int slot, const StoredPokemon* mon) {
    if (slot < 0 || slot >= PARTY_LENGTH) return;

    StoredPokemon* party = (gen == GEN_1) ? gen1Party : gen2Party;
    const char* prefix = (gen == GEN_1) ? "g1_x" : "g2_x";

    memcpy(&party[slot], mon, sizeof(StoredPokemon));
    party[slot].occupied = true;
    saveSlotNVS(prefix, slot, mon);

    Serial.printf("[STORAGE] Saved %s slot %d (species=0x%02X)\n",
                  gen == GEN_1 ? "Gen1" : "Gen2", slot, mon->speciesIndex);
}

void storage_clearSlot(Generation gen, int slot) {
    if (slot < 0 || slot >= PARTY_LENGTH) return;

    StoredPokemon* party = (gen == GEN_1) ? gen1Party : gen2Party;
    const char* prefix = (gen == GEN_1) ? "g1_x" : "g2_x";

    memset(&party[slot], 0, sizeof(StoredPokemon));
    party[slot].occupied = false;
    clearSlotNVS(prefix, slot);

    Serial.printf("[STORAGE] Cleared %s slot %d\n",
                  gen == GEN_1 ? "Gen1" : "Gen2", slot);
}

int storage_getCount(Generation gen) {
    StoredPokemon* party = (gen == GEN_1) ? gen1Party : gen2Party;
    int count = 0;
    for (int i = 0; i < PARTY_LENGTH; i++) {
        if (party[i].occupied) count++;
    }
    return count;
}

StoredPokemon* storage_getParty(Generation gen) {
    return (gen == GEN_1) ? gen1Party : gen2Party;
}

void storage_setTradeMode(TradeMode mode) {
    prefs.putUChar("mode", (uint8_t)mode);
    Serial.printf("[STORAGE] Trade mode set to %s\n",
                  mode == TRADE_MODE_CLONE ? "clone" : "storage");
}

TradeMode storage_getTradeMode() {
    return (TradeMode)prefs.getUChar("mode", (uint8_t)TRADE_MODE_CLONE);
}
