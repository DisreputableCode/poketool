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

    debug_logf("[TRADE] Prepared %s party (%d data bytes, mode=%s)\n",
               gen == GEN_1 ? "Gen1" : "Gen2",
               dataLength,
               (TradeMode)ctx.tradeMode == TRADE_MODE_CLONE ? "clone" : "storage");
}

// =============================================================================
// Save Received Pokemon to NVS
// =============================================================================

static void saveReceivedPokemon() {
    if (tradePokemon < 0 || tradePokemon >= PARTY_LENGTH) return;

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
        debug_logf("[TRADE] Received Gen1: %s (idx=0x%02X) Lv%d\n",
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
        debug_logf("[TRADE] Received Gen2: %s (dex=%d) Lv%d\n",
                   gen2_getSpeciesName(mon->species), mon->species, mon->level);
    }

    TradeMode mode = (TradeMode)ctx.tradeMode;
    int saveSlot;

    if (mode == TRADE_MODE_CLONE) {
        saveSlot = 0;
    } else {
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
    int count = recvBlock[11];
    if (count > PARTY_LENGTH) count = PARTY_LENGTH;

    ctx.opponentCount = count;

    debug_logf("[TRADE] Opponent party (%d Pokemon):\n", count);

    for (int i = 0; i < count; i++) {
        if (gen == GEN_1) {
            int monOff = 19 + i * GEN1_PARTY_STRUCT_SIZE;
            int nickOff = 349 + i * NAME_LENGTH;
            Gen1PartyMon* mon = (Gen1PartyMon*)(recvBlock + monOff);
            ctx.opponentSpecies[i] = mon->species;
            ctx.opponentLevels[i] = mon->level;
            memcpy((void*)ctx.opponentNicknames[i], recvBlock + nickOff, NAME_LENGTH);
            debug_logf("  [%d] %s (idx=0x%02X) Lv%d HP=%d\n",
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
            debug_logf("  [%d] %s (dex=%d) Lv%d HP=%d\n",
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
        debug_logf("[CONN] Disconnected (was %s)\n", connStateName(prev));
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
            debug_logf("[CONN] Connected (Gen 1)\n");
            led_setPattern(LED_DOUBLE_BLINK);
        } else if (in == PKMN_CONNECTED_GEN2) {
            send = PKMN_CONNECTED_GEN2;
            connState = CONN_CONNECTED;
            gen = GEN_2;
            debug_logf("[CONN] Connected (Gen 2)\n");
            led_setPattern(LED_DOUBLE_BLINK);
        } else {
            send = in;
        }
        break;

    // =========================================================================
    // CONNECTED: Menu navigation
    // Gen 1 menu: Trade Centre (D4), Colosseum (D5), Cancel (D6)
    // Gen 2 menu: Trade Centre (D4), Colosseum (D5), Time Capsule (D6)
    // Both gens send D0/D1/D2 for menu highlights.
    // =========================================================================
    case CONN_CONNECTED:
        if (in == ITEM_1_HIGHLIGHTED || in == ITEM_2_HIGHLIGHTED || in == ITEM_3_HIGHLIGHTED) {
            // Menu highlight — echo back, don't change gen
            send = in;
        } else if (in == TRADE_CENTRE) {
            // D4: Trade Centre (native format for current gen)
            connState = CONN_TRADE_CENTRE;
            tcState = TC_INIT;
            debug_logf("[CONN] -> TRADE_CENTRE (%s)\n", gen == GEN_1 ? "Gen1" : "Gen2");
            led_setPattern(LED_TRIPLE_BLINK);
        } else if (in == COLOSSEUM) {
            connState = CONN_COLOSSEUM;
            debug_logf("[CONN] -> COLOSSEUM (echoing)\n");
        } else if (in == BREAK_LINK) {
            if (gen == GEN_2) {
                // D6 in Gen 2 = Time Capsule (switch to Gen 1 format)
                gen = GEN_1;
                connState = CONN_TRADE_CENTRE;
                tcState = TC_INIT;
                send = in;
                debug_logf("[CONN] -> TIME CAPSULE (Gen1 format)\n");
                led_setPattern(LED_TRIPLE_BLINK);
            } else {
                // D6 in Gen 1 = Cancel/Break Link
                resetConnection();
                send = BREAK_LINK;
            }
        } else if (in == PKMN_MASTER) {
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
                debug_logf("[TC] INIT -> READY_TO_GO\n");
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
                debug_logf("[TC] SENDING_DATA (0/%d)\n", dataLength);
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
                debug_logf("[TC] Data exchange complete (%d bytes)\n", counter);
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
                    debug_logf("[TC] Patch exchange complete -> TRADE_PENDING\n");
                }
            }
            break;

        case TC_TRADE_PENDING:
            if ((in & 0x60) == 0x60) {
                if (in == 0x6F) {
                    tcState = TC_READY_TO_GO;
                    send = 0x6F;
                    debug_logf("[TC] Trade cancelled -> READY_TO_GO\n");
                } else {
                    tradePokemon = in - TRADE_POKEMON_BASE;
                    send = TRADE_POKEMON_BASE + ctx.offerSlot;
                    debug_logf("[TC] GB selected %d, we offer %d\n", tradePokemon, ctx.offerSlot);
                }
            } else if (in == 0x00) {
                send = 0x00;
                tcState = TC_TRADE_CONFIRMATION;
                debug_logf("[TC] -> TRADE_CONFIRMATION\n");
            } else {
                send = in;
            }
            break;

        case TC_TRADE_CONFIRMATION:
            if ((in & 0x60) == 0x60) {
                if (in == 0x61) {
                    tradePokemon = -1;
                    tcState = TC_TRADE_PENDING;
                    send = in;
                    debug_logf("[TC] Trade declined by GB -> TRADE_PENDING\n");
                } else {
                    if (ctx.autoConfirm) {
                        send = 0x62;
                        tcState = TC_DONE;
                        debug_logf("[TC] Trade auto-confirmed -> DONE\n");
                    } else if (ctx.confirmRequested) {
                        ctx.confirmRequested = false;
                        send = 0x62;
                        tcState = TC_DONE;
                        debug_logf("[TC] Trade confirmed (manual) -> DONE\n");
                    } else {
                        send = 0x61;
                        tradePokemon = -1;
                        tcState = TC_TRADE_PENDING;
                        ctx.declineRequested = false;
                        debug_logf("[TC] Trade declined (manual) -> TRADE_PENDING\n");
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
                debug_logf("[TC] DONE -> INIT (ready for next trade)\n");
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

    debug_logf("=== PokeTool v0.3 ===\n");
    debug_logf("Pins: MOSI=%d MISO=%d SCLK=%d LED=%d\n",
               PIN_MOSI, PIN_MISO, PIN_SCLK, PIN_LED);

    link_init();
    led_init();
    led_setPattern(LED_SLOW_BLINK);

    storage_init();

    memset(&ctx, 0, sizeof(ctx));
    ctx.tradeMode = (int)storage_getTradeMode();
    ctx.offerSlot = 0;
    ctx.autoConfirm = true;
    ctx.tradePokemon = -1;

    wifi_init(&ctx);

    resetConnection();

    debug_logf("Ready. Connect to WiFi 'PokeTool' -> 192.168.4.1\n");
}

void loop() {
    led_update();

    int received = link_transferByte(outByte);

    if (received < 0) {
        // Flush any pending SPI debug data during idle
        debug_spi_flush();

        if (link_isIdle(IDLE_TIMEOUT_MS)) {
            if (tradePokemon >= 0 && tcState < TC_TRADE_PENDING) {
                saveReceivedPokemon();
            }

            if (connState != CONN_NOT_CONNECTED) {
                resetConnection();
            }
        }
        return;
    }

    // Log SPI byte exchange
    debug_spi(outByte, (uint8_t)received);

    outByte = handleByte((uint8_t)received);

    delayMicroseconds(BYTE_DELAY_US);
}
