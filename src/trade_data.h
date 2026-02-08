#ifndef TRADE_DATA_H
#define TRADE_DATA_H

#include "config.h"

// =============================================================================
// Gen 1 Pokemon Data Structures (from pokered constants/pokemon_data_constants.asm)
// =============================================================================

// Gen 1 party Pokemon structure: 44 bytes (0x2C)
// box_struct (33 bytes) + battle stats (11 bytes)
struct Gen1PartyMon {
    // --- box_struct (33 bytes, 0x00-0x20) ---
    uint8_t species;        // 0x00
    uint8_t hp[2];          // 0x01 (big-endian)
    uint8_t boxLevel;       // 0x03
    uint8_t status;         // 0x04
    uint8_t type1;          // 0x05
    uint8_t type2;          // 0x06
    uint8_t catchRate;      // 0x07
    uint8_t moves[4];       // 0x08
    uint8_t trainerId[2];   // 0x0C (big-endian)
    uint8_t exp[3];         // 0x0E (big-endian)
    uint8_t hpEV[2];        // 0x11
    uint8_t atkEV[2];       // 0x13
    uint8_t defEV[2];       // 0x15
    uint8_t spdEV[2];       // 0x17
    uint8_t spcEV[2];       // 0x19
    uint8_t dvs[2];         // 0x1B
    uint8_t pp[4];          // 0x1D
    // --- battle stats (11 bytes, 0x21-0x2B) ---
    uint8_t level;          // 0x21
    uint8_t maxHp[2];       // 0x22
    uint8_t atk[2];         // 0x24
    uint8_t def[2];         // 0x26
    uint8_t spd[2];         // 0x28
    uint8_t spc[2];         // 0x2A
} __attribute__((packed));

static_assert(sizeof(Gen1PartyMon) == GEN1_PARTY_STRUCT_SIZE,
              "Gen1PartyMon must be 44 bytes");

// Gen 1 full party data block: 424 bytes
struct Gen1PartyBlock {
    uint8_t preamble[GEN1_PREAMBLE_SIZE];           // 6 x 0xFD
    uint8_t playerName[NAME_LENGTH];                 // 11 bytes
    uint8_t partyCount;                              // 1 byte
    uint8_t partySpecies[PARTY_LENGTH + 1];          // 7 bytes (6 + 0xFF term)
    Gen1PartyMon pokemon[PARTY_LENGTH];              // 264 bytes
    uint8_t otNames[PARTY_LENGTH][NAME_LENGTH];      // 66 bytes
    uint8_t nicknames[PARTY_LENGTH][NAME_LENGTH];    // 66 bytes
    uint8_t padding[3];                              // 3 bytes
} __attribute__((packed));

static_assert(sizeof(Gen1PartyBlock) == GEN1_PARTY_BLOCK_SIZE,
              "Gen1PartyBlock must be 424 bytes");

// =============================================================================
// Gen 2 Pokemon Data Structures (from pokecrystal constants/pokemon_data_constants.asm)
// =============================================================================

// Gen 2 party Pokemon structure: 48 bytes (0x30)
// box_struct (32 bytes, 0x00-0x1F) + battle stats (16 bytes, 0x20-0x2F)
struct Gen2PartyMon {
    // --- box_struct (32 bytes, 0x00-0x1F) ---
    uint8_t species;        // 0x00
    uint8_t item;           // 0x01
    uint8_t moves[4];       // 0x02
    uint8_t trainerId[2];   // 0x06 (big-endian)
    uint8_t exp[3];         // 0x08 (big-endian)
    uint8_t hpEV[2];        // 0x0B
    uint8_t atkEV[2];       // 0x0D
    uint8_t defEV[2];       // 0x0F
    uint8_t spdEV[2];       // 0x11
    uint8_t spcEV[2];       // 0x13
    uint8_t dvs[2];         // 0x15
    uint8_t pp[4];          // 0x17
    uint8_t happiness;      // 0x1B
    uint8_t pokerus;        // 0x1C
    uint8_t caughtData[2];  // 0x1D (time/level in byte0, gender/location in byte1)
    uint8_t level;          // 0x1F
    // --- battle stats (16 bytes, 0x20-0x2F) ---
    uint8_t status;         // 0x20
    uint8_t unused;         // 0x21
    uint8_t hp[2];          // 0x22 (big-endian)
    uint8_t maxHp[2];       // 0x24
    uint8_t atk[2];         // 0x26
    uint8_t def[2];         // 0x28
    uint8_t spd[2];         // 0x2A
    uint8_t spAtk[2];       // 0x2C
    uint8_t spDef[2];       // 0x2E
} __attribute__((packed));

static_assert(sizeof(Gen2PartyMon) == GEN2_PARTY_STRUCT_SIZE,
              "Gen2PartyMon must be 48 bytes");

// Gen 2 full party data block: 450 bytes
// 6 + 11 + 1 + 7 + 2 + (48*6) + (11*6) + (11*6) + 3
struct Gen2PartyBlock {
    uint8_t preamble[GEN2_PREAMBLE_SIZE];           // 6 x 0xFD
    uint8_t playerName[NAME_LENGTH];                 // 11 bytes
    uint8_t partyCount;                              // 1 byte
    uint8_t partySpecies[PARTY_LENGTH + 1];          // 7 bytes
    uint8_t playerId[2];                             // 2 bytes (extra in Gen 2)
    Gen2PartyMon pokemon[PARTY_LENGTH];              // 288 bytes
    uint8_t otNames[PARTY_LENGTH][NAME_LENGTH];      // 66 bytes
    uint8_t nicknames[PARTY_LENGTH][NAME_LENGTH];    // 66 bytes
    uint8_t padding[3];                              // 3 bytes
} __attribute__((packed));

static_assert(sizeof(Gen2PartyBlock) == GEN2_PARTY_BLOCK_SIZE,
              "Gen2PartyBlock must be 450 bytes");

// =============================================================================
// Wire protocol buffers
// =============================================================================

#define MAX_PARTY_BLOCK_SIZE  GEN2_PARTY_BLOCK_SIZE  // 450 (Gen 2 is larger)

struct RandomBlock {
    uint8_t data[GEN1_RANDOM_BLOCK_SIZE]; // 17 bytes
} __attribute__((packed));

struct PatchList {
    uint8_t data[GEN1_PATCH_LIST_SIZE]; // 200 bytes
} __attribute__((packed));

// =============================================================================
// Patch list utilities
// =============================================================================

// Build a patch list for outgoing data. Scans for 0xFE bytes, records offsets
// in the patch list, and replaces them with 0xFF in the data.
// splitOffset = PATCH_DATA_SPLIT (252) — the patch list is split into two parts.
void buildPatchList(uint8_t* data, uint16_t dataLen, uint8_t* patchList,
                    uint16_t splitOffset);

// Apply a received patch list: restore 0xFE bytes at recorded offsets.
void applyPatchList(uint8_t* data, uint16_t dataLen, const uint8_t* patchList);

// =============================================================================
// Pokemon name tables (species index -> display name)
// =============================================================================

// Gen 1 uses a non-sequential internal index. Returns name or "???" for unknown.
const char* gen1_getSpeciesName(uint8_t internalIndex);

// Gen 2 uses Pokedex order (1-251). Returns name or "???" for unknown.
const char* gen2_getSpeciesName(uint8_t dexNum);

// =============================================================================
// Default party builders (for first clone trade when no data stored)
// =============================================================================

void gen1_buildDefaultParty(Gen1PartyBlock* block);
void gen2_buildDefaultParty(Gen2PartyBlock* block);

#endif // TRADE_DATA_H
