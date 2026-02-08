#include <Arduino.h>
#include "config.h"
#include "link_cable.h"
#include "led.h"
#include "trade_data.h"
#include "storage.h"
#include "wifi_server.h"
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

// =============================================================================
// Global State
// =============================================================================

static ConnectionState connState = CONN_NOT_CONNECTED;
static TradeCentreState tcState = TC_INIT;
static Generation gen = GEN_UNKNOWN;

// Shared context for web server
static TradeContext ctx;

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
static int tradePokemon = -1;

// Storage mode: maps party position -> storage slot index
static int partyToStorage[PARTY_LENGTH];

// The current byte to send (set by handleByte, consumed by loop)
static uint8_t outByte = 0x00;

// =============================================================================
// Sync State to TradeContext (for web server visibility)
// =============================================================================

static void syncContext() {
    ctx.connState = (int)connState;
    ctx.tcState = (int)tcState;
    ctx.gen = (int)gen;
    ctx.tradePokemon = tradePokemon;
}

// =============================================================================
// Prepare Trade Data — Mode-aware party building
// =============================================================================

static void prepareTradeData() {
    StoredPokemon* party = storage_getParty(gen);
    TradeMode mode = (TradeMode)ctx.tradeMode;

    if (gen == GEN_1) {
        dataLength = GEN1_PARTY_BLOCK_SIZE - GEN1_PREAMBLE_SIZE; // 418

        Gen1PartyBlock* block = (Gen1PartyBlock*)sendBlock;
        memset(block, 0, sizeof(Gen1PartyBlock));
        memset(block->preamble, SERIAL_PREAMBLE_BYTE, GEN1_PREAMBLE_SIZE);

        if (mode == TRADE_MODE_CLONE) {
            // Clone mode: clone slot 0 into all 6 positions
            if (party[0].occupied) {
                memcpy(block->playerName, party[0].ot, NAME_LENGTH);
                block->partyCount = PARTY_LENGTH;
                for (int i = 0; i < PARTY_LENGTH; i++) {
                    block->partySpecies[i] = party[0].speciesIndex;
                    memcpy(&block->pokemon[i], party[0].monData, GEN1_PARTY_STRUCT_SIZE);
                    memcpy(block->otNames[i], party[0].ot, NAME_LENGTH);
                    memcpy(block->nicknames[i], party[0].nickname, NAME_LENGTH);
                    partyToStorage[i] = 0;
                }
                block->partySpecies[PARTY_LENGTH] = 0xFF;
            } else {
                gen1_buildDefaultParty(block);
                for (int i = 0; i < PARTY_LENGTH; i++) partyToStorage[i] = -1;
            }
        } else {
            // Storage mode: fill with occupied slots
            int pos = 0;
            for (int i = 0; i < PARTY_LENGTH && pos < PARTY_LENGTH; i++) {
                if (party[i].occupied) {
                    block->partySpecies[pos] = party[i].speciesIndex;
                    memcpy(&block->pokemon[pos], party[i].monData, GEN1_PARTY_STRUCT_SIZE);
                    memcpy(block->otNames[pos], party[i].ot, NAME_LENGTH);
                    memcpy(block->nicknames[pos], party[i].nickname, NAME_LENGTH);
                    partyToStorage[pos] = i;
                    pos++;
                }
            }
            if (pos == 0) {
                gen1_buildDefaultParty(block);
                for (int i = 0; i < PARTY_LENGTH; i++) partyToStorage[i] = -1;
            } else {
                // Use first occupied slot's OT as player name
                memcpy(block->playerName, party[partyToStorage[0]].ot, NAME_LENGTH);
                block->partyCount = pos;
                block->partySpecies[pos] = 0xFF;
                for (int i = pos; i < PARTY_LENGTH; i++) partyToStorage[i] = -1;
            }
        }

        buildPatchList(sendBlock + GEN1_PREAMBLE_SIZE, dataLength,
                       sendPatch, PATCH_DATA_SPLIT);

    } else { // GEN_2
        dataLength = GEN2_PARTY_BLOCK_SIZE - GEN2_PREAMBLE_SIZE; // 444

        Gen2PartyBlock* block = (Gen2PartyBlock*)sendBlock;
        memset(block, 0, sizeof(Gen2PartyBlock));
        memset(block->preamble, SERIAL_PREAMBLE_BYTE, GEN2_PREAMBLE_SIZE);
        block->playerId[0] = 0x00;
        block->playerId[1] = 0x01;

        if (mode == TRADE_MODE_CLONE) {
            if (party[0].occupied) {
                memcpy(block->playerName, party[0].ot, NAME_LENGTH);
                block->partyCount = PARTY_LENGTH;
                for (int i = 0; i < PARTY_LENGTH; i++) {
                    block->partySpecies[i] = party[0].speciesIndex;
                    memcpy(&block->pokemon[i], party[0].monData, GEN2_PARTY_STRUCT_SIZE);
                    memcpy(block->otNames[i], party[0].ot, NAME_LENGTH);
                    memcpy(block->nicknames[i], party[0].nickname, NAME_LENGTH);
                    partyToStorage[i] = 0;
                }
                block->partySpecies[PARTY_LENGTH] = 0xFF;
            } else {
                gen2_buildDefaultParty(block);
                for (int i = 0; i < PARTY_LENGTH; i++) partyToStorage[i] = -1;
            }
        } else {
            int pos = 0;
            for (int i = 0; i < PARTY_LENGTH && pos < PARTY_LENGTH; i++) {
                if (party[i].occupied) {
                    block->partySpecies[pos] = party[i].speciesIndex;
                    memcpy(&block->pokemon[pos], party[i].monData, GEN2_PARTY_STRUCT_SIZE);
                    memcpy(block->otNames[pos], party[i].ot, NAME_LENGTH);
                    memcpy(block->nicknames[pos], party[i].nickname, NAME_LENGTH);
                    partyToStorage[pos] = i;
                    pos++;
                }
            }
            if (pos == 0) {
                gen2_buildDefaultParty(block);
                for (int i = 0; i < PARTY_LENGTH; i++) partyToStorage[i] = -1;
            } else {
                memcpy(block->playerName, party[partyToStorage[0]].ot, NAME_LENGTH);
                block->partyCount = pos;
                block->partySpecies[pos] = 0xFF;
                for (int i = pos; i < PARTY_LENGTH; i++) partyToStorage[i] = -1;
            }
        }

        buildPatchList(sendBlock + GEN2_PREAMBLE_SIZE, dataLength,
                       sendPatch, PATCH_DATA_SPLIT);
    }

    Serial.printf("[TRADE] Prepared %s party (%d data bytes, mode=%s)\n",
                  gen == GEN_1 ? "Gen1" : "Gen2",
                  dataLength,
                  (TradeMode)ctx.tradeMode == TRADE_MODE_CLONE ? "clone" : "storage");
}

// =============================================================================
// Save Received Pokemon to NVS
// =============================================================================

static void saveReceivedPokemon() {
    if (tradePokemon < 0 || tradePokemon >= PARTY_LENGTH) return;

    // Apply patch list to restore 0xFE bytes in received data
    applyPatchList(recvBlock, dataLength, recvPatch);

    StoredPokemon received;
    memset(&received, 0, sizeof(received));
    received.occupied = true;

    int monSize, monOffset, otOffset, nickOffset;

    if (gen == GEN_1) {
        monSize = GEN1_PARTY_STRUCT_SIZE;
        monOffset = 19 + (tradePokemon * monSize);
        otOffset = 283 + (tradePokemon * NAME_LENGTH);
        nickOffset = 349 + (tradePokemon * NAME_LENGTH);
        received.speciesIndex = recvBlock[12 + tradePokemon];

        memcpy(received.monData, recvBlock + monOffset, monSize);
        memcpy(received.ot, recvBlock + otOffset, NAME_LENGTH);
        memcpy(received.nickname, recvBlock + nickOffset, NAME_LENGTH);

        Gen1PartyMon* mon = (Gen1PartyMon*)(recvBlock + monOffset);
        Serial.printf("[TRADE] Received Gen1 Pokemon: %s (idx=0x%02X) Lv%d\n",
                      gen1_getSpeciesName(mon->species), mon->species, mon->level);
    } else {
        monSize = GEN2_PARTY_STRUCT_SIZE;
        monOffset = 21 + (tradePokemon * monSize);
        otOffset = 309 + (tradePokemon * NAME_LENGTH);
        nickOffset = 375 + (tradePokemon * NAME_LENGTH);
        received.speciesIndex = recvBlock[12 + tradePokemon];

        memcpy(received.monData, recvBlock + monOffset, monSize);
        memcpy(received.ot, recvBlock + otOffset, NAME_LENGTH);
        memcpy(received.nickname, recvBlock + nickOffset, NAME_LENGTH);

        Gen2PartyMon* mon = (Gen2PartyMon*)(recvBlock + monOffset);
        Serial.printf("[TRADE] Received Gen2 Pokemon: %s (dex=%d) Lv%d\n",
                      gen2_getSpeciesName(mon->species), mon->species, mon->level);
    }

    // Determine save slot
    TradeMode mode = (TradeMode)ctx.tradeMode;
    int saveSlot;

    if (mode == TRADE_MODE_CLONE) {
        saveSlot = 0;
    } else {
        // Storage mode: save to the storage slot that was traded away
        int offerPos = ctx.offerSlot;
        saveSlot = (offerPos >= 0 && offerPos < PARTY_LENGTH && partyToStorage[offerPos] >= 0)
                   ? partyToStorage[offerPos] : 0;
    }

    storage_saveSlot(gen, saveSlot, &received);
    tradePokemon = -1;
}

// =============================================================================
// Log Received Party Summary + populate TradeContext opponent info
// =============================================================================

static void logReceivedParty() {
    int count = recvBlock[11]; // partyCount at offset 11 in data portion
    if (count > PARTY_LENGTH) count = PARTY_LENGTH;

    ctx.opponentCount = count;

    Serial.printf("[TRADE] Opponent party (%d Pokemon):\n", count);

    for (int i = 0; i < count; i++) {
        if (gen == GEN_1) {
            int monOff = 19 + i * GEN1_PARTY_STRUCT_SIZE;
            int nickOff = 349 + i * NAME_LENGTH;
            Gen1PartyMon* mon = (Gen1PartyMon*)(recvBlock + monOff);
            ctx.opponentSpecies[i] = mon->species;
            ctx.opponentLevels[i] = mon->level;
            memcpy((void*)ctx.opponentNicknames[i], recvBlock + nickOff, NAME_LENGTH);
            Serial.printf("  [%d] %s (idx=0x%02X) Lv%d HP=%d\n",
                          i, gen1_getSpeciesName(mon->species),
                          mon->species, mon->level,
                          (mon->hp[0] << 8) | mon->hp[1]);
        } else {
            int monOff = 21 + i * GEN2_PARTY_STRUCT_SIZE;
            int nickOff = 375 + i * NAME_LENGTH;
            Gen2PartyMon* mon = (Gen2PartyMon*)(recvBlock + monOff);
            ctx.opponentSpecies[i] = mon->species;
            ctx.opponentLevels[i] = mon->level;
            memcpy((void*)ctx.opponentNicknames[i], recvBlock + nickOff, NAME_LENGTH);
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
    ctx.opponentCount = 0;
    ctx.tradePokemon = -1;
    ctx.confirmRequested = false;
    ctx.declineRequested = false;

    syncContext();

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
            send = in;
        }
        break;

    // =========================================================================
    // CONNECTED: Menu navigation
    // =========================================================================
    case CONN_CONNECTED:
        if (in == ITEM_1_HIGHLIGHTED || in == ITEM_2_HIGHLIGHTED || in == ITEM_3_HIGHLIGHTED) {
            if (gen == GEN_2) {
                gen = GEN_1;
                Serial.println("[CONN] Gen 2 Time Capsule detected (switching to Gen 1 format)");
            }
            send = in;
        } else if (in == TRADE_CENTRE) {
            if (gen == GEN_2) gen = GEN_1;
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
            send = in;
        } else {
            send = in;
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
                tcState = TC_SENDING_RANDOM_DATA;
                send = in;
                counter = 0;
            } else {
                send = SERIAL_PREAMBLE_BYTE;
            }
            break;

        case TC_SENDING_RANDOM_DATA:
            if (in == SERIAL_PREAMBLE_BYTE) {
                tcState = TC_WAITING_TO_SEND_DATA;
                send = SERIAL_PREAMBLE_BYTE;
                prepareTradeData();
            } else {
                send = in;
            }
            break;

        case TC_WAITING_TO_SEND_DATA:
            if (in != SERIAL_PREAMBLE_BYTE) {
                counter = 0;
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
                counter = 0;
                send = SERIAL_PREAMBLE_BYTE;
            } else {
                send = sendPatch[3 + counter];
                recvPatch[3 + counter] = in;
                counter++;
                if (counter >= 197) {
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
                    tcState = TC_READY_TO_GO;
                    send = 0x6F;
                    Serial.println("[TC] Trade cancelled -> READY_TO_GO");
                } else {
                    // Game Boy selected a Pokemon
                    tradePokemon = in - TRADE_POKEMON_BASE;
                    // Offer the slot selected via web UI
                    send = TRADE_POKEMON_BASE + ctx.offerSlot;
                    Serial.printf("[TC] GB selected %d, we offer %d\n", tradePokemon, ctx.offerSlot);
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
                    // GB declined
                    tradePokemon = -1;
                    tcState = TC_TRADE_PENDING;
                    send = in;
                    Serial.println("[TC] Trade declined by GB -> TRADE_PENDING");
                } else {
                    // GB confirmed (0x62) — check our response
                    if (ctx.autoConfirm) {
                        send = 0x62;
                        tcState = TC_DONE;
                        Serial.println("[TC] Trade auto-confirmed -> DONE");
                    } else if (ctx.confirmRequested) {
                        ctx.confirmRequested = false;
                        send = 0x62;
                        tcState = TC_DONE;
                        Serial.println("[TC] Trade confirmed (manual) -> DONE");
                    } else {
                        // Decline — go back to pending
                        send = 0x61;
                        tradePokemon = -1;
                        tcState = TC_TRADE_PENDING;
                        ctx.declineRequested = false;
                        Serial.println("[TC] Trade declined (manual) -> TRADE_PENDING");
                    }
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
    // COLOSSEUM: Just echo
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

    syncContext();
    return send;
}

// =============================================================================
// Arduino Entry Points
// =============================================================================

void setup() {
    Serial.begin(115200);
    delay(1000);

    Serial.println("=== PokeTool v0.2 ===");
    Serial.printf("Pins: MOSI=%d MISO=%d SCLK=%d LED=%d\n",
                  PIN_MOSI, PIN_MISO, PIN_SCLK, PIN_LED);

    link_init();
    led_init();
    led_setPattern(LED_SLOW_BLINK);

    // Initialize NVS storage
    storage_init();

    // Initialize trade context
    memset(&ctx, 0, sizeof(ctx));
    ctx.tradeMode = (int)storage_getTradeMode();
    ctx.offerSlot = 0;
    ctx.autoConfirm = true;
    ctx.tradePokemon = -1;

    // Start WiFi AP + web server
    wifi_init(&ctx);

    resetConnection();

    Serial.println("Ready. Connect to WiFi 'PokeTool' -> 192.168.4.1");
}

void loop() {
    led_update();

    int received = link_transferByte(outByte);

    if (received < 0) {
        if (link_isIdle(IDLE_TIMEOUT_MS)) {
            // Save received Pokemon if a trade was completed
            if (tradePokemon >= 0 && tcState < TC_TRADE_PENDING) {
                saveReceivedPokemon();
            }

            if (connState != CONN_NOT_CONNECTED) {
                resetConnection();
            }
        }
        return;
    }

    outByte = handleByte((uint8_t)received);

    delayMicroseconds(BYTE_DELAY_US);
}
