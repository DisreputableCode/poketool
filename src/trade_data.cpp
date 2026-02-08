#include "trade_data.h"
#include <string.h>

// =============================================================================
// Patch List Implementation
// (from pokered home/serial.asm FixDataForLinkTransfer / ApplyPatchList)
// =============================================================================

void buildPatchList(uint8_t* data, uint16_t dataLen, uint8_t* patchList,
                    uint16_t splitOffset) {
    // Patch list format:
    //   [3 bytes preamble: 0xFD 0xFD 0xFD]
    //   [part 1 offsets...] [0xFF terminator]
    //   [part 2 offsets...] [0xFF terminator]
    // Part 1 covers data[0..splitOffset-1], Part 2 covers data[splitOffset..]

    memset(patchList, 0, GEN1_PATCH_LIST_SIZE);

    // Preamble
    patchList[0] = SERIAL_PREAMBLE_BYTE;
    patchList[1] = SERIAL_PREAMBLE_BYTE;
    patchList[2] = SERIAL_PREAMBLE_BYTE;

    uint16_t patchIdx = 3;

    // Part 1: scan data[0..splitOffset-1]
    uint16_t end1 = (splitOffset < dataLen) ? splitOffset : dataLen;
    for (uint16_t i = 0; i < end1 && patchIdx < GEN1_PATCH_LIST_SIZE - 2; i++) {
        if (data[i] == SERIAL_NO_DATA_BYTE) {
            patchList[patchIdx++] = i + 1; // Offset is 1-indexed
            data[i] = SERIAL_PATCH_TERM;   // Replace with 0xFF
        }
    }
    if (patchIdx < GEN1_PATCH_LIST_SIZE - 1) {
        patchList[patchIdx++] = SERIAL_PATCH_TERM; // Part 1 terminator
    }

    // Part 2: scan data[splitOffset..dataLen-1]
    for (uint16_t i = splitOffset; i < dataLen && patchIdx < GEN1_PATCH_LIST_SIZE - 1; i++) {
        if (data[i] == SERIAL_NO_DATA_BYTE) {
            patchList[patchIdx++] = (i - splitOffset) + 1; // Relative, 1-indexed
            data[i] = SERIAL_PATCH_TERM;
        }
    }
    if (patchIdx < GEN1_PATCH_LIST_SIZE) {
        patchList[patchIdx] = SERIAL_PATCH_TERM; // Part 2 terminator
    }
}

void applyPatchList(uint8_t* data, uint16_t dataLen, const uint8_t* patchList) {
    // Skip preamble bytes at start of patch list
    uint16_t patchIdx = 0;
    while (patchIdx < GEN1_PATCH_LIST_SIZE &&
           patchList[patchIdx] == SERIAL_PREAMBLE_BYTE) {
        patchIdx++;
    }

    bool inPart2 = false;
    uint16_t baseOffset = 0;

    while (patchIdx < GEN1_PATCH_LIST_SIZE) {
        uint8_t val = patchList[patchIdx++];

        if (val == SERIAL_PATCH_TERM) {
            if (inPart2) break; // Done
            inPart2 = true;
            baseOffset = PATCH_DATA_SPLIT;
            continue;
        }
        if (val == 0 || val == SERIAL_PREAMBLE_BYTE || val == SERIAL_NO_DATA_BYTE) {
            continue; // Skip special bytes
        }

        uint16_t offset = baseOffset + (val - 1); // 1-indexed back to 0-indexed
        if (offset < dataLen) {
            data[offset] = SERIAL_NO_DATA_BYTE; // Restore 0xFE
        }
    }
}

// =============================================================================
// Gen 1 Species Name Table
// Gen 1 uses a non-sequential internal index. This maps internal ID -> name.
// Index 0 = no pokemon. Indices sourced from pokered constants.
// =============================================================================

static const char* const GEN1_SPECIES_NAMES[] = {
    "???",         // 0x00
    "Rhydon",      // 0x01
    "Kangaskhan",  // 0x02
    "Nidoran M",   // 0x03
    "Clefairy",    // 0x04
    "Spearow",     // 0x05
    "Voltorb",     // 0x06
    "Nidoking",    // 0x07
    "Slowbro",     // 0x08
    "Ivysaur",     // 0x09
    "Exeggutor",   // 0x0A
    "Lickitung",   // 0x0B
    "Exeggcute",   // 0x0C
    "Grimer",      // 0x0D
    "Gengar",      // 0x0E
    "Nidoran F",   // 0x0F
    "Nidoqueen",   // 0x10
    "Cubone",      // 0x11
    "Rhyhorn",     // 0x12
    "Lapras",      // 0x13
    "Arcanine",    // 0x14
    "Mew",         // 0x15
    "Gyarados",    // 0x16
    "Shellder",    // 0x17
    "Tentacool",   // 0x18
    "Gastly",      // 0x19
    "Scyther",     // 0x1A
    "Staryu",      // 0x1B
    "Blastoise",   // 0x1C
    "Pinsir",      // 0x1D
    "Tangela",     // 0x1E
    "???",         // 0x1F (MissingNo)
    "???",         // 0x20 (MissingNo)
    "Growlithe",   // 0x21
    "Onix",        // 0x22
    "Fearow",      // 0x23
    "Pidgey",      // 0x24
    "Slowpoke",    // 0x25
    "Kadabra",     // 0x26
    "Graveler",    // 0x27
    "Chansey",     // 0x28
    "Machoke",     // 0x29
    "Mr. Mime",    // 0x2A
    "Hitmonlee",   // 0x2B
    "Hitmonchan",  // 0x2C
    "Arbok",       // 0x2D
    "Parasect",    // 0x2E
    "Psyduck",     // 0x2F
    "Drowzee",     // 0x30
    "Golem",       // 0x31
    "???",         // 0x32 (MissingNo)
    "Magmar",      // 0x33
    "???",         // 0x34 (MissingNo)
    "Electabuzz",  // 0x35
    "Magneton",    // 0x36
    "Koffing",     // 0x37
    "???",         // 0x38 (MissingNo)
    "Mankey",      // 0x39
    "Seel",        // 0x3A
    "Diglett",     // 0x3B
    "Tauros",      // 0x3C
    "???",         // 0x3D-0x3F (MissingNo)
    "???", "???",
    "Farfetch'd",  // 0x40
    "Venonat",     // 0x41
    "Dragonite",   // 0x42
    "???", "???", "???", // 0x43-0x45
    "Doduo",       // 0x46
    "Poliwag",     // 0x47
    "Jynx",        // 0x48
    "Moltres",     // 0x49
    "Articuno",    // 0x4A
    "Zapdos",      // 0x4B
    "Ditto",       // 0x4C
    "Meowth",      // 0x4D
    "Krabby",      // 0x4E
    "???", "???", "???", // 0x4F-0x51
    "Vulpix",      // 0x52
    "Ninetales",   // 0x53
    "Pikachu",     // 0x54
    "Raichu",      // 0x55
    "???", "???",  // 0x56-0x57
    "Dratini",     // 0x58
    "Dragonair",   // 0x59
    "Kabuto",      // 0x5A
    "Kabutops",    // 0x5B
    "Horsea",      // 0x5C
    "Seadra",      // 0x5D
    "???", "???",  // 0x5E-0x5F
    "Sandshrew",   // 0x60
    "Sandslash",   // 0x61
    "Omanyte",     // 0x62
    "Omastar",     // 0x63
    "Jigglypuff",  // 0x64
    "Wigglytuff",  // 0x65
    "Eevee",       // 0x66
    "Flareon",     // 0x67
    "Jolteon",     // 0x68
    "Vaporeon",    // 0x69
    "Machop",      // 0x6A
    "Zubat",       // 0x6B
    "Ekans",       // 0x6C
    "Paras",       // 0x6D
    "Poliwhirl",   // 0x6E
    "Poliwrath",   // 0x6F
    "Weedle",      // 0x70
    "Kakuna",      // 0x71
    "Beedrill",    // 0x72
    "???",         // 0x73
    "Dodrio",      // 0x74
    "Primeape",    // 0x75
    "Dugtrio",     // 0x76
    "Venomoth",    // 0x77
    "Dewgong",     // 0x78
    "???", "???",  // 0x79-0x7A
    "Caterpie",    // 0x7B
    "Metapod",     // 0x7C
    "Butterfree",  // 0x7D
    "Machamp",     // 0x7E
    "???",         // 0x7F
    "Golduck",     // 0x80
    "Hypno",       // 0x81
    "Golbat",      // 0x82
    "Mewtwo",      // 0x83
    "Snorlax",     // 0x84
    "Magikarp",    // 0x85
    "???", "???",  // 0x86-0x87
    "Muk",         // 0x88
    "???",         // 0x89
    "Kingler",     // 0x8A
    "Cloyster",    // 0x8B
    "???",         // 0x8C
    "Electrode",   // 0x8D
    "Clefable",    // 0x8E
    "Weezing",     // 0x8F
    "Persian",     // 0x90
    "Marowak",     // 0x91
    "???",         // 0x92
    "Haunter",     // 0x93
    "Abra",        // 0x94
    "Alakazam",    // 0x95
    "Pidgeotto",   // 0x96
    "Pidgeot",     // 0x97
    "Starmie",     // 0x98
    "Bulbasaur",   // 0x99
    "Venusaur",    // 0x9A
    "Tentacruel",  // 0x9B
    "???",         // 0x9C
    "Goldeen",     // 0x9D
    "Seaking",     // 0x9E
    "???", "???", "???", "???", // 0x9F-0xA2
    "Ponyta",      // 0xA3
    "Rapidash",    // 0xA4
    "Rattata",     // 0xA5
    "Raticate",    // 0xA6
    "Nidorino",    // 0xA7
    "Nidorina",    // 0xA8
    "Geodude",     // 0xA9
    "Porygon",     // 0xAA
    "Aerodactyl",  // 0xAB
    "???",         // 0xAC
    "Magnemite",   // 0xAD
    "???", "???",  // 0xAE-0xAF
    "Charmander",  // 0xB0
    "Squirtle",    // 0xB1
    "Charmeleon",  // 0xB2
    "Wartortle",   // 0xB3
    "Charizard",   // 0xB4
    "???", "???", "???", "???", // 0xB5-0xB8
    "Oddish",      // 0xB9
    "Gloom",       // 0xBA
    "Vileplume",   // 0xBB
    "Bellsprout",  // 0xBC
    "Weepinbell",  // 0xBD
    "Victreebel",  // 0xBE
};

#define GEN1_SPECIES_TABLE_SIZE (sizeof(GEN1_SPECIES_NAMES) / sizeof(GEN1_SPECIES_NAMES[0]))

const char* gen1_getSpeciesName(uint8_t internalIndex) {
    if (internalIndex == 0 || internalIndex >= GEN1_SPECIES_TABLE_SIZE) {
        return "???";
    }
    return GEN1_SPECIES_NAMES[internalIndex];
}

// =============================================================================
// Gen 2 Species Name Table
// Gen 2 uses Pokedex order (1 = Bulbasaur, 251 = Celebi)
// =============================================================================

static const char* const GEN2_SPECIES_NAMES[] = {
    "???",          // 0
    "Bulbasaur",    // 1
    "Ivysaur",      // 2
    "Venusaur",     // 3
    "Charmander",   // 4
    "Charmeleon",   // 5
    "Charizard",    // 6
    "Squirtle",     // 7
    "Wartortle",    // 8
    "Blastoise",    // 9
    "Caterpie",     // 10
    "Metapod",      // 11
    "Butterfree",   // 12
    "Weedle",       // 13
    "Kakuna",       // 14
    "Beedrill",     // 15
    "Pidgey",       // 16
    "Pidgeotto",    // 17
    "Pidgeot",      // 18
    "Rattata",      // 19
    "Raticate",     // 20
    "Spearow",      // 21
    "Fearow",       // 22
    "Ekans",        // 23
    "Arbok",        // 24
    "Pikachu",      // 25
    "Raichu",       // 26
    "Sandshrew",    // 27
    "Sandslash",    // 28
    "Nidoran F",    // 29
    "Nidorina",     // 30
    "Nidoqueen",    // 31
    "Nidoran M",    // 32
    "Nidorino",     // 33
    "Nidoking",     // 34
    "Clefairy",     // 35
    "Clefable",     // 36
    "Vulpix",       // 37
    "Ninetales",    // 38
    "Jigglypuff",   // 39
    "Wigglytuff",   // 40
    "Zubat",        // 41
    "Golbat",       // 42
    "Oddish",       // 43
    "Gloom",        // 44
    "Vileplume",    // 45
    "Paras",        // 46
    "Parasect",     // 47
    "Venonat",      // 48
    "Venomoth",     // 49
    "Diglett",      // 50
    "Dugtrio",      // 51
    "Meowth",       // 52
    "Persian",      // 53
    "Psyduck",      // 54
    "Golduck",      // 55
    "Mankey",       // 56
    "Primeape",     // 57
    "Growlithe",    // 58
    "Arcanine",     // 59
    "Poliwag",      // 60
    "Poliwhirl",    // 61
    "Poliwrath",    // 62
    "Abra",         // 63
    "Kadabra",      // 64
    "Alakazam",     // 65
    "Machop",       // 66
    "Machoke",      // 67
    "Machamp",      // 68
    "Bellsprout",   // 69
    "Weepinbell",   // 70
    "Victreebel",   // 71
    "Tentacool",    // 72
    "Tentacruel",   // 73
    "Geodude",      // 74
    "Graveler",     // 75
    "Golem",        // 76
    "Ponyta",       // 77
    "Rapidash",     // 78
    "Slowpoke",     // 79
    "Slowbro",      // 80
    "Magnemite",    // 81
    "Magneton",     // 82
    "Farfetch'd",   // 83
    "Doduo",        // 84
    "Dodrio",       // 85
    "Seel",         // 86
    "Dewgong",      // 87
    "Grimer",       // 88
    "Muk",          // 89
    "Shellder",     // 90
    "Cloyster",     // 91
    "Gastly",       // 92
    "Haunter",      // 93
    "Gengar",       // 94
    "Onix",         // 95
    "Drowzee",      // 96
    "Hypno",        // 97
    "Krabby",       // 98
    "Kingler",      // 99
    "Voltorb",      // 100
    "Electrode",    // 101
    "Exeggcute",    // 102
    "Exeggutor",    // 103
    "Cubone",       // 104
    "Marowak",      // 105
    "Hitmonlee",    // 106
    "Hitmonchan",   // 107
    "Lickitung",    // 108
    "Koffing",      // 109
    "Weezing",      // 110
    "Rhyhorn",      // 111
    "Rhydon",       // 112
    "Chansey",      // 113
    "Tangela",      // 114
    "Kangaskhan",   // 115
    "Horsea",       // 116
    "Seadra",       // 117
    "Goldeen",      // 118
    "Seaking",      // 119
    "Staryu",       // 120
    "Starmie",      // 121
    "Mr. Mime",     // 122
    "Scyther",      // 123
    "Jynx",         // 124
    "Electabuzz",   // 125
    "Magmar",       // 126
    "Pinsir",       // 127
    "Tauros",       // 128
    "Magikarp",     // 129
    "Gyarados",     // 130
    "Lapras",       // 131
    "Ditto",        // 132
    "Eevee",        // 133
    "Vaporeon",     // 134
    "Jolteon",      // 135
    "Flareon",      // 136
    "Porygon",      // 137
    "Omanyte",      // 138
    "Omastar",      // 139
    "Kabuto",       // 140
    "Kabutops",     // 141
    "Aerodactyl",   // 142
    "Snorlax",      // 143
    "Articuno",     // 144
    "Zapdos",       // 145
    "Moltres",      // 146
    "Dratini",      // 147
    "Dragonair",    // 148
    "Dragonite",    // 149
    "Mewtwo",       // 150
    "Mew",          // 151
    "Chikorita",    // 152
    "Bayleef",      // 153
    "Meganium",     // 154
    "Cyndaquil",    // 155
    "Quilava",      // 156
    "Typhlosion",   // 157
    "Totodile",     // 158
    "Croconaw",     // 159
    "Feraligatr",   // 160
    "Sentret",      // 161
    "Furret",       // 162
    "Hoothoot",     // 163
    "Noctowl",      // 164
    "Ledyba",       // 165
    "Ledian",       // 166
    "Spinarak",     // 167
    "Ariados",      // 168
    "Crobat",       // 169
    "Chinchou",     // 170
    "Lanturn",      // 171
    "Pichu",        // 172
    "Cleffa",       // 173
    "Igglybuff",    // 174
    "Togepi",       // 175
    "Togetic",      // 176
    "Natu",         // 177
    "Xatu",         // 178
    "Mareep",       // 179
    "Flaaffy",      // 180
    "Ampharos",     // 181
    "Bellossom",    // 182
    "Marill",       // 183
    "Azumarill",    // 184
    "Sudowoodo",    // 185
    "Politoed",     // 186
    "Hoppip",       // 187
    "Skiploom",     // 188
    "Jumpluff",     // 189
    "Aipom",        // 190
    "Sunkern",      // 191
    "Sunflora",     // 192
    "Yanma",        // 193
    "Wooper",       // 194
    "Quagsire",     // 195
    "Espeon",       // 196
    "Umbreon",      // 197
    "Murkrow",      // 198
    "Slowking",     // 199
    "Misdreavus",   // 200
    "Unown",        // 201
    "Wobbuffet",    // 202
    "Girafarig",    // 203
    "Pineco",       // 204
    "Forretress",   // 205
    "Dunsparce",    // 206
    "Gligar",       // 207
    "Steelix",      // 208
    "Snubbull",     // 209
    "Granbull",     // 210
    "Qwilfish",     // 211
    "Scizor",       // 212
    "Shuckle",      // 213
    "Heracross",    // 214
    "Sneasel",      // 215
    "Teddiursa",    // 216
    "Ursaring",     // 217
    "Slugma",       // 218
    "Magcargo",     // 219
    "Swinub",       // 220
    "Piloswine",    // 221
    "Corsola",      // 222
    "Remoraid",     // 223
    "Octillery",    // 224
    "Delibird",     // 225
    "Mantine",      // 226
    "Skarmory",     // 227
    "Houndour",     // 228
    "Houndoom",     // 229
    "Kingdra",      // 230
    "Phanpy",       // 231
    "Donphan",      // 232
    "Porygon2",     // 233
    "Stantler",     // 234
    "Smeargle",     // 235
    "Tyrogue",      // 236
    "Hitmontop",    // 237
    "Smoochum",     // 238
    "Elekid",       // 239
    "Magby",        // 240
    "Miltank",      // 241
    "Blissey",      // 242
    "Raikou",       // 243
    "Entei",        // 244
    "Suicune",      // 245
    "Larvitar",     // 246
    "Pupitar",      // 247
    "Tyranitar",    // 248
    "Lugia",        // 249
    "Ho-Oh",        // 250
    "Celebi",       // 251
};

#define GEN2_SPECIES_TABLE_SIZE (sizeof(GEN2_SPECIES_NAMES) / sizeof(GEN2_SPECIES_NAMES[0]))

const char* gen2_getSpeciesName(uint8_t dexNum) {
    if (dexNum == 0 || dexNum >= GEN2_SPECIES_TABLE_SIZE) {
        return "???";
    }
    return GEN2_SPECIES_NAMES[dexNum];
}

// =============================================================================
// Default Party Builders
// =============================================================================

// Game Boy text encoding: A=0x80, a=0xA0, 0x50=terminator
static const uint8_t NAME_PKMN[] = {
    0x8F, 0x8E, 0x8A, 0x8D, 0x8D, 0x50, 0x50, 0x50, 0x50, 0x50, 0x50
    // "POKEMN" + terminators (simplified trainer name)
};

static const uint8_t NICKNAME_BULBA[] = {
    0x81, 0x94, 0x8B, 0x81, 0x80, 0x92, 0x80, 0x94, 0x91, 0x50, 0x50
    // "BULBASAUR" + terminator
};

static const uint8_t NICKNAME_CHIKO[] = {
    0x82, 0x87, 0x88, 0x8A, 0x8E, 0x91, 0x88, 0x93, 0x80, 0x50, 0x50
    // "CHIKORITA" + terminator
};

void gen1_buildDefaultParty(Gen1PartyBlock* block) {
    memset(block, 0, sizeof(Gen1PartyBlock));

    // Preamble
    memset(block->preamble, SERIAL_PREAMBLE_BYTE, GEN1_PREAMBLE_SIZE);

    // Trainer name
    memcpy(block->playerName, NAME_PKMN, NAME_LENGTH);

    // Party: 1 Bulbasaur
    block->partyCount = 1;
    block->partySpecies[0] = 0x99; // Bulbasaur internal index in Gen 1
    block->partySpecies[1] = 0xFF; // Terminator

    // Bulbasaur at level 5
    Gen1PartyMon* mon = &block->pokemon[0];
    mon->species = 0x99;
    mon->hp[0] = 0x00; mon->hp[1] = 0x14;  // 20 HP
    mon->boxLevel = 5;
    mon->type1 = 0x16; // Grass
    mon->type2 = 0x03; // Poison
    mon->catchRate = 45;
    mon->moves[0] = 0x21; // Tackle
    mon->moves[1] = 0x2D; // Growl
    mon->trainerId[0] = 0x00; mon->trainerId[1] = 0x01;
    mon->exp[0] = 0x00; mon->exp[1] = 0x00; mon->exp[2] = 125;
    mon->dvs[0] = 0xAA; mon->dvs[1] = 0xAA;
    mon->pp[0] = 35; // Tackle PP
    mon->pp[1] = 40; // Growl PP
    mon->level = 5;
    mon->maxHp[0] = 0x00; mon->maxHp[1] = 0x14;
    mon->atk[0] = 0x00; mon->atk[1] = 0x09;
    mon->def[0] = 0x00; mon->def[1] = 0x09;
    mon->spd[0] = 0x00; mon->spd[1] = 0x08;
    mon->spc[0] = 0x00; mon->spc[1] = 0x0A;

    // OT name and nickname
    memcpy(block->otNames[0], NAME_PKMN, NAME_LENGTH);
    memcpy(block->nicknames[0], NICKNAME_BULBA, NAME_LENGTH);

    // Fill remaining species slots with 0xFF
    for (int i = 2; i <= PARTY_LENGTH; i++) {
        block->partySpecies[i] = 0xFF;
    }
}

void gen2_buildDefaultParty(Gen2PartyBlock* block) {
    memset(block, 0, sizeof(Gen2PartyBlock));

    // Preamble
    memset(block->preamble, SERIAL_PREAMBLE_BYTE, GEN2_PREAMBLE_SIZE);

    // Trainer name
    memcpy(block->playerName, NAME_PKMN, NAME_LENGTH);

    // Party: 1 Chikorita
    block->partyCount = 1;
    block->partySpecies[0] = 152; // Chikorita dex number
    block->partySpecies[1] = 0xFF;

    block->playerId[0] = 0x00;
    block->playerId[1] = 0x01;

    // Chikorita at level 5
    Gen2PartyMon* mon = &block->pokemon[0];
    mon->species = 152;
    mon->item = 0;
    mon->moves[0] = 0x21; // Tackle
    mon->moves[1] = 0x2D; // Growl
    mon->trainerId[0] = 0x00; mon->trainerId[1] = 0x01;
    mon->exp[0] = 0x00; mon->exp[1] = 0x00; mon->exp[2] = 125;
    mon->dvs[0] = 0xAA; mon->dvs[1] = 0xAA;
    mon->pp[0] = 35;
    mon->pp[1] = 40;
    mon->happiness = 70;
    mon->level = 5;
    mon->hp[0] = 0x00; mon->hp[1] = 0x14;
    mon->maxHp[0] = 0x00; mon->maxHp[1] = 0x14;
    mon->atk[0] = 0x00; mon->atk[1] = 0x09;
    mon->def[0] = 0x00; mon->def[1] = 0x0A;
    mon->spd[0] = 0x00; mon->spd[1] = 0x08;
    mon->spAtk[0] = 0x00; mon->spAtk[1] = 0x09;
    mon->spDef[0] = 0x00; mon->spDef[1] = 0x0A;

    // OT name and nickname
    memcpy(block->otNames[0], NAME_PKMN, NAME_LENGTH);
    memcpy(block->nicknames[0], NICKNAME_CHIKO, NAME_LENGTH);

    for (int i = 2; i <= PARTY_LENGTH; i++) {
        block->partySpecies[i] = 0xFF;
    }
}
