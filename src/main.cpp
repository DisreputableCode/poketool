#include <Arduino.h>
#include "config.h"
#include "link_cable.h"
#include "led.h"
#include "trade_data.h"
#include <string.h>

// =============================================================================
// Connection State Machine
// =============================================================================

enum ConnectionState {
    CONN_NOT_CONNECTED,
    CONN_CONNECTED,
    CONN_TRADE_CENTRE,
    CONN_COLOSSEUM
};

// =============================================================================
// Trade Centre State Machine
// =============================================================================

enum TradeCentreState {
    TC_INIT,
    TC_READY_TO_GO,
    TC_SEEN_FIRST_WAIT,
    TC_SENDING_RANDOM_DATA,
    TC_WAITING_TO_SEND_DATA,
    TC_SENDING_DATA,
    TC_SENDING_PATCH_DATA,
    TC_TRADE_PENDING,
    TC_TRADE_CONFIRMATION,
    TC_DONE
};

static const char* connStateName(ConnectionState s) {
    switch (s) {
        case CONN_NOT_CONNECTED: return "NOT_CONNECTED";
        case CONN_CONNECTED:     return "CONNECTED";
        case CONN_TRADE_CENTRE:  return "TRADE_CENTRE";
        case CONN_COLOSSEUM:     return "COLOSSEUM";
    }
    return "?";
}

static const char* tcStateName(TradeCentreState s) {
    switch (s) {
        case TC_INIT:                return "INIT";
        case TC_READY_TO_GO:         return "READY_TO_GO";
        case TC_SEEN_FIRST_WAIT:     return "SEEN_FIRST_WAIT";
        case TC_SENDING_RANDOM_DATA: return "SENDING_RANDOM_DATA";
        case TC_WAITING_TO_SEND_DATA:return "WAIT_TO_SEND";
        case TC_SENDING_DATA:        return "SENDING_DATA";
        case TC_SENDING_PATCH_DATA:  return "SENDING_PATCH";
        case TC_TRADE_PENDING:       return "TRADE_PENDING";
        case TC_TRADE_CONFIRMATION:  return "TRADE_CONFIRM";
        case TC_DONE:                return "DONE";
    }
    return "?";
}

// =============================================================================
// Global State
// =============================================================================

static ConnectionState connState = CONN_NOT_CONNECTED;
static TradeCentreState tcState = TC_INIT;
static Generation gen = GEN_UNKNOWN;

// Data exchange buffers (sized for Gen 2 which is larger)
static uint8_t sendBlock[MAX_PARTY_BLOCK_SIZE];
static uint8_t recvBlock[MAX_PARTY_BLOCK_SIZE];
static uint8_t sendPatch[GEN1_PATCH_LIST_SIZE];
static uint8_t recvPatch[GEN1_PATCH_LIST_SIZE];

// Exchange counter for SENDING_DATA and SENDING_PATCH_DATA
static int counter = 0;

// Data length for current generation (excludes 6-byte preamble)
static int dataLength = 0;

// Trade tracking
static int tradePokemon = -1;       // Index of Pokemon selected by Game Boy (-1 = none)
static bool haveReceivedPokemon = false; // Have we received a Pokemon from a previous trade?

// Received Pokemon storage (single slot for clone mode)
// Gen 1: 44 bytes struct + 11 OT name + 11 nickname = 66 bytes
// Gen 2: 48 bytes struct + 11 OT name + 11 nickname = 70 bytes
static uint8_t storedMon[GEN2_PARTY_STRUCT_SIZE];
static uint8_t storedOT[NAME_LENGTH];
static uint8_t storedNickname[NAME_LENGTH];
static uint8_t storedSpeciesIndex = 0; // Species byte for partySpecies array

// The current byte to send (set by handleByte, consumed by loop)
static uint8_t outByte = 0x00;

// =============================================================================
// Prepare Trade Data
// =============================================================================

// Build our party block and patch list for sending.
// On first trade: default party. On subsequent: clone received Pokemon.
static void prepareTradeData() {
    if (gen == GEN_1) {
        dataLength = GEN1_PARTY_BLOCK_SIZE - GEN1_PREAMBLE_SIZE; // 418

        Gen1PartyBlock* block = (Gen1PartyBlock*)sendBlock;

        if (haveReceivedPokemon) {
            // Clone mode: fill party with received Pokemon
            memset(block, 0, sizeof(Gen1PartyBlock));
            memset(block->preamble, SERIAL_PREAMBLE_BYTE, GEN1_PREAMBLE_SIZE);
            memcpy(block->playerName, storedOT, NAME_LENGTH);
            block->partyCount = PARTY_LENGTH;
            for (int i = 0; i < PARTY_LENGTH; i++) {
                block->partySpecies[i] = storedSpeciesIndex;
                memcpy(&block->pokemon[i], storedMon, GEN1_PARTY_STRUCT_SIZE);
                memcpy(block->otNames[i], storedOT, NAME_LENGTH);
                memcpy(block->nicknames[i], storedNickname, NAME_LENGTH);
            }
            block->partySpecies[PARTY_LENGTH] = 0xFF;
        } else {
            gen1_buildDefaultParty(block);
        }

        // Build patch list on data portion (skip preamble)
        buildPatchList(sendBlock + GEN1_PREAMBLE_SIZE, dataLength,
                       sendPatch, PATCH_DATA_SPLIT);

    } else { // GEN_2
        dataLength = GEN2_PARTY_BLOCK_SIZE - GEN2_PREAMBLE_SIZE; // 444

        Gen2PartyBlock* block = (Gen2PartyBlock*)sendBlock;

        if (haveReceivedPokemon) {
            memset(block, 0, sizeof(Gen2PartyBlock));
            memset(block->preamble, SERIAL_PREAMBLE_BYTE, GEN2_PREAMBLE_SIZE);
            memcpy(block->playerName, storedOT, NAME_LENGTH);
            block->partyCount = PARTY_LENGTH;
            block->playerId[0] = 0x00;
            block->playerId[1] = 0x01;
            for (int i = 0; i < PARTY_LENGTH; i++) {
                block->partySpecies[i] = storedSpeciesIndex;
                memcpy(&block->pokemon[i], storedMon, GEN2_PARTY_STRUCT_SIZE);
                memcpy(block->otNames[i], storedOT, NAME_LENGTH);
                memcpy(block->nicknames[i], storedNickname, NAME_LENGTH);
            }
            block->partySpecies[PARTY_LENGTH] = 0xFF;
        } else {
            gen2_buildDefaultParty(block);
        }

        buildPatchList(sendBlock + GEN2_PREAMBLE_SIZE, dataLength,
                       sendPatch, PATCH_DATA_SPLIT);
    }

    Serial.printf("[TRADE] Prepared %s party (%d data bytes, %s)\n",
                  gen == GEN_1 ? "Gen1" : "Gen2",
                  dataLength,
                  haveReceivedPokemon ? "clone" : "default");
}

// =============================================================================
// Save Received Pokemon
// =============================================================================

static void saveReceivedPokemon() {
    if (tradePokemon < 0 || tradePokemon >= PARTY_LENGTH) return;

    // Apply patch list to restore 0xFE bytes in received data
    applyPatchList(recvBlock, dataLength, recvPatch);

    int monSize, monOffset, otOffset, nickOffset;

    if (gen == GEN_1) {
        // Layout within data portion (after preamble, which we didn't store):
        // [0..10] playerName (11)
        // [11] partyCount (1)
        // [12..18] partySpecies (7)
        // [19..282] pokemon[6] (44*6=264)
        // [283..348] otNames[6] (11*6=66)
        // [349..414] nicknames[6] (11*6=66)
        // [415..417] padding (3)
        monSize = GEN1_PARTY_STRUCT_SIZE;
        monOffset = 19 + (tradePokemon * monSize);
        otOffset = 283 + (tradePokemon * NAME_LENGTH);
        nickOffset = 349 + (tradePokemon * NAME_LENGTH);
        storedSpeciesIndex = recvBlock[12 + tradePokemon]; // partySpecies[i] starts at offset 12

        memcpy(storedMon, recvBlock + monOffset, monSize);
        memcpy(storedOT, recvBlock + otOffset, NAME_LENGTH);
        memcpy(storedNickname, recvBlock + nickOffset, NAME_LENGTH);

        // Log what we received
        Gen1PartyMon* mon = (Gen1PartyMon*)(recvBlock + monOffset);
        Serial.printf("[TRADE] Received Gen1 Pokemon: %s (idx=0x%02X) Lv%d\n",
                      gen1_getSpeciesName(mon->species), mon->species, mon->level);

    } else {
        // Gen 2 layout within data portion:
        // [0..10] playerName (11)
        // [11] partyCount (1)
        // [12..18] partySpecies (7)
        // [19..20] playerId (2)
        // [21..308] pokemon[6] (48*6=288)
        // [309..374] otNames[6] (11*6=66)
        // [375..440] nicknames[6] (11*6=66)
        // [441..443] padding (3)
        monSize = GEN2_PARTY_STRUCT_SIZE;
        monOffset = 21 + (tradePokemon * monSize);
        otOffset = 309 + (tradePokemon * NAME_LENGTH);
        nickOffset = 375 + (tradePokemon * NAME_LENGTH);
        storedSpeciesIndex = recvBlock[12 + tradePokemon];

        memcpy(storedMon, recvBlock + monOffset, monSize);
        memcpy(storedOT, recvBlock + otOffset, NAME_LENGTH);
        memcpy(storedNickname, recvBlock + nickOffset, NAME_LENGTH);

        Gen2PartyMon* mon = (Gen2PartyMon*)(recvBlock + monOffset);
        Serial.printf("[TRADE] Received Gen2 Pokemon: %s (dex=%d) Lv%d\n",
                      gen2_getSpeciesName(mon->species), mon->species, mon->level);
    }

    haveReceivedPokemon = true;
    tradePokemon = -1;
}

// =============================================================================
// Log Received Party Summary
// =============================================================================

static void logReceivedParty() {
    int count = recvBlock[11]; // partyCount at offset 11 in data portion
    if (count > PARTY_LENGTH) count = PARTY_LENGTH;

    Serial.printf("[TRADE] Opponent party (%d Pokemon):\n", count);

    for (int i = 0; i < count; i++) {
        if (gen == GEN_1) {
            Gen1PartyMon* mon = (Gen1PartyMon*)(recvBlock + 19 + i * GEN1_PARTY_STRUCT_SIZE);
            Serial.printf("  [%d] %s (idx=0x%02X) Lv%d HP=%d\n",
                          i, gen1_getSpeciesName(mon->species),
                          mon->species, mon->level,
                          (mon->hp[0] << 8) | mon->hp[1]);
        } else {
            Gen2PartyMon* mon = (Gen2PartyMon*)(recvBlock + 21 + i * GEN2_PARTY_STRUCT_SIZE);
            Serial.printf("  [%d] %s (dex=%d) Lv%d HP=%d\n",
                          i, gen2_getSpeciesName(mon->species),
                          mon->species, mon->level,
                          (mon->hp[0] << 8) | mon->hp[1]);
        }
    }
}

// =============================================================================
// Reset State
// =============================================================================

static void resetConnection() {
    ConnectionState prev = connState;
    connState = CONN_NOT_CONNECTED;
    tcState = TC_INIT;
    gen = GEN_UNKNOWN;
    counter = 0;
    dataLength = 0;
    outByte = 0x00;

    if (prev != CONN_NOT_CONNECTED) {
        Serial.printf("[CONN] Disconnected (was %s)\n", connStateName(prev));
    }

    led_setPattern(LED_SLOW_BLINK);
}

// =============================================================================
// Handle Incoming Byte — Main Protocol State Machine
// =============================================================================

static uint8_t handleByte(uint8_t in) {
    uint8_t send = 0x00;

    switch (connState) {

    // =========================================================================
    // NOT_CONNECTED: Handshake
    // =========================================================================
    case CONN_NOT_CONNECTED:
        if (in == PKMN_MASTER) {
            send = PKMN_SLAVE;
        } else if (in == PKMN_BLANK) {
            send = PKMN_BLANK;
        } else if (in == PKMN_CONNECTED) {
            send = PKMN_CONNECTED;
            connState = CONN_CONNECTED;
            gen = GEN_1;
            Serial.println("[CONN] Connected (Gen 1)");
            led_setPattern(LED_DOUBLE_BLINK);
        } else if (in == PKMN_CONNECTED_GEN2) {
            send = PKMN_CONNECTED_GEN2;
            connState = CONN_CONNECTED;
            gen = GEN_2;
            Serial.println("[CONN] Connected (Gen 2)");
            led_setPattern(LED_DOUBLE_BLINK);
        } else {
            send = in; // Echo unknown bytes
        }
        break;

    // =========================================================================
    // CONNECTED: Menu navigation, waiting for Trade Centre / Colosseum selection
    // =========================================================================
    case CONN_CONNECTED:
        if (in == ITEM_1_HIGHLIGHTED || in == ITEM_2_HIGHLIGHTED || in == ITEM_3_HIGHLIGHTED) {
            // Menu highlight — if Gen 2 sent this, it's using Gen 1 protocol (Time Capsule)
            if (gen == GEN_2) {
                gen = GEN_1;
                Serial.println("[CONN] Gen 2 Time Capsule detected (switching to Gen 1 format)");
            }
            send = in;
        } else if (in == TRADE_CENTRE) {
            if (gen == GEN_2) gen = GEN_1; // Time Capsule
            connState = CONN_TRADE_CENTRE;
            tcState = TC_INIT;
            Serial.println("[CONN] -> TRADE_CENTRE");
            led_setPattern(LED_TRIPLE_BLINK);
        } else if (in == COLOSSEUM) {
            if (gen == GEN_2) gen = GEN_1;
            connState = CONN_COLOSSEUM;
            Serial.println("[CONN] -> COLOSSEUM (echoing)");
        } else if (in == BREAK_LINK || in == PKMN_MASTER) {
            resetConnection();
            send = BREAK_LINK;
        } else if (in == PKMN_CONNECTED || in == PKMN_CONNECTED_GEN2) {
            send = in; // Keep-alive echo
        } else {
            send = in; // Echo
        }
        break;

    // =========================================================================
    // TRADE_CENTRE: The main trade protocol state machine
    // =========================================================================
    case CONN_TRADE_CENTRE:
        switch (tcState) {

        case TC_INIT:
            if (in == 0x00) {
                tcState = TC_READY_TO_GO;
                send = 0x00;
                Serial.println("[TC] INIT -> READY_TO_GO");
            } else {
                send = in;
            }
            break;

        case TC_READY_TO_GO:
            if (in == SERIAL_PREAMBLE_BYTE) {
                tcState = TC_SEEN_FIRST_WAIT;
                send = SERIAL_PREAMBLE_BYTE;
            } else {
                send = in;
            }
            break;

        case TC_SEEN_FIRST_WAIT:
            if (in != SERIAL_PREAMBLE_BYTE) {
                // First non-preamble byte = start of random data
                tcState = TC_SENDING_RANDOM_DATA;
                send = in; // Echo random data (slave's random is ignored)
                counter = 0;
            } else {
                send = SERIAL_PREAMBLE_BYTE;
            }
            break;

        case TC_SENDING_RANDOM_DATA:
            if (in == SERIAL_PREAMBLE_BYTE) {
                // Preamble for data block
                tcState = TC_WAITING_TO_SEND_DATA;
                send = SERIAL_PREAMBLE_BYTE;
                prepareTradeData();
            } else {
                send = in; // Echo random data
            }
            break;

        case TC_WAITING_TO_SEND_DATA:
            if (in != SERIAL_PREAMBLE_BYTE) {
                // First data byte — start exchange
                counter = 0;
                // Send our first data byte (offset past preamble)
                send = sendBlock[GEN1_PREAMBLE_SIZE + counter];
                recvBlock[counter] = in;
                counter++;
                tcState = TC_SENDING_DATA;
                Serial.printf("[TC] SENDING_DATA (0/%d)\n", dataLength);
            } else {
                send = SERIAL_PREAMBLE_BYTE;
            }
            break;

        case TC_SENDING_DATA:
            send = sendBlock[GEN1_PREAMBLE_SIZE + counter];
            recvBlock[counter] = in;
            counter++;
            if (counter >= dataLength) {
                tcState = TC_SENDING_PATCH_DATA;
                Serial.printf("[TC] Data exchange complete (%d bytes)\n", counter);
                logReceivedParty();
            }
            break;

        case TC_SENDING_PATCH_DATA:
            if (in == SERIAL_PREAMBLE_BYTE) {
                // Consume patch preamble bytes
                counter = 0;
                send = SERIAL_PREAMBLE_BYTE;
            } else {
                // Exchange patch data (echo back — we don't need to send our own
                // patch corrections since buildPatchList already patched our data)
                send = sendPatch[3 + counter]; // Skip our 3-byte preamble
                recvPatch[3 + counter] = in;
                counter++;
                if (counter >= 197) {
                    // Fill in preamble bytes we consumed
                    recvPatch[0] = SERIAL_PREAMBLE_BYTE;
                    recvPatch[1] = SERIAL_PREAMBLE_BYTE;
                    recvPatch[2] = SERIAL_PREAMBLE_BYTE;
                    tcState = TC_TRADE_PENDING;
                    Serial.println("[TC] Patch exchange complete -> TRADE_PENDING");
                }
            }
            break;

        case TC_TRADE_PENDING:
            if ((in & 0x60) == 0x60) {
                if (in == 0x6F) {
                    // Cancel / back to menu
                    tcState = TC_READY_TO_GO;
                    send = 0x6F;
                    Serial.println("[TC] Trade cancelled -> READY_TO_GO");
                } else {
                    // Game Boy selected a Pokemon (0x60 + index)
                    tradePokemon = in - TRADE_POKEMON_BASE;
                    send = TRADE_POKEMON_BASE; // We always offer Pokemon 0
                    Serial.printf("[TC] GB selected Pokemon %d, we offer 0\n", tradePokemon);
                }
            } else if (in == 0x00) {
                send = 0x00;
                tcState = TC_TRADE_CONFIRMATION;
                Serial.println("[TC] -> TRADE_CONFIRMATION");
            } else {
                send = in;
            }
            break;

        case TC_TRADE_CONFIRMATION:
            if ((in & 0x60) == 0x60) {
                if (in == 0x61) {
                    // Trade declined
                    tradePokemon = -1;
                    tcState = TC_TRADE_PENDING;
                    send = in;
                    Serial.println("[TC] Trade declined -> TRADE_PENDING");
                } else {
                    // Trade confirmed (0x62)
                    send = 0x62; // We always confirm
                    tcState = TC_DONE;
                    Serial.println("[TC] Trade confirmed! -> DONE");
                }
            } else {
                send = in;
            }
            break;

        case TC_DONE:
            if (in == 0x00) {
                send = 0x00;
                tcState = TC_INIT;
                Serial.println("[TC] DONE -> INIT (ready for next trade)");
            } else {
                send = in;
            }
            break;
        }
        break;

    // =========================================================================
    // COLOSSEUM: Just echo (not implementing battle)
    // =========================================================================
    case CONN_COLOSSEUM:
        if (in == BREAK_LINK || in == PKMN_MASTER) {
            resetConnection();
            send = BREAK_LINK;
        } else {
            send = in;
        }
        break;
    }

    return send;
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000); // Wait for USB CDC serial

    Serial.println("=== PokeTool v0.1 ===");
    Serial.printf("Pins: MOSI=%d MISO=%d SCLK=%d LED=%d\n",
                  PIN_MOSI, PIN_MISO, PIN_SCLK, PIN_LED);

    link_init();
    led_init();
    led_setPattern(LED_SLOW_BLINK);

    resetConnection();

    Serial.println("Ready. Waiting for Game Boy...");
}

void loop() {
    led_update();

    // Exchange a byte with the Game Boy
    int received = link_transferByte(outByte);

    if (received < 0) {
        // Timeout — check if we should do idle bookkeeping
        if (link_isIdle(IDLE_TIMEOUT_MS)) {
            // Save received Pokemon if a trade was completed
            if (tradePokemon >= 0 && tcState < TC_TRADE_PENDING) {
                saveReceivedPokemon();
            }

            // Reset if we were connected
            if (connState != CONN_NOT_CONNECTED) {
                resetConnection();
            }
        }
        return;
    }

    // Process the received byte and get our next send byte
    outByte = handleByte((uint8_t)received);

    delayMicroseconds(BYTE_DELAY_US);
}
