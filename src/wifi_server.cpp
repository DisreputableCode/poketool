#include "wifi_server.h"
#include "storage.h"
#include "trade_data.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>
#include <stdarg.h>

// =============================================================================
// WiFi + Web Server Implementation
// =============================================================================

static AsyncWebServer server(80);
static AsyncEventSource events("/events");
static TradeContext* ctx = nullptr;

// Connection state names (must match enum order in main.cpp)
static const char* CONN_NAMES[] = {
    "not_connected", "connected", "trade_centre", "colosseum"
};

// Trade centre state names
static const char* TC_NAMES[] = {
    "init", "ready_to_go", "seen_first_wait", "sending_random",
    "wait_to_send", "sending_data", "sending_patch",
    "trade_pending", "trade_confirm", "done"
};

static const char* genName(int g) {
    if (g == GEN_1) return "gen1";
    if (g == GEN_2) return "gen2";
    return "unknown";
}

// Helper: get species name for either generation
static const char* speciesName(int gen, uint8_t species) {
    if (gen == GEN_1) return gen1_getSpeciesName(species);
    return gen2_getSpeciesName(species);
}

// Helper: decode a Game Boy text string to ASCII (for nicknames)
static void gbTextToAscii(const uint8_t* src, char* dst, int maxLen) {
    for (int i = 0; i < maxLen; i++) {
        uint8_t c = src[i];
        if (c == 0x50) { dst[i] = '\0'; return; }  // Terminator
        else if (c >= 0x80 && c <= 0x99) dst[i] = 'A' + (c - 0x80); // A-Z
        else if (c >= 0xA0 && c <= 0xB9) dst[i] = 'a' + (c - 0xA0); // a-z
        else if (c == 0xE8) dst[i] = '\'';
        else if (c == 0xE3) dst[i] = '-';
        else if (c == 0x7F) dst[i] = ' ';
        else if (c == 0xF2) dst[i] = '.';
        else if (c == 0xEF) dst[i] = 'M'; // Male symbol -> M
        else if (c == 0xF5) dst[i] = 'F'; // Female symbol -> F
        else dst[i] = '?';
    }
    dst[maxLen - 1] = '\0';
}

// =============================================================================
// Debug Logging
// =============================================================================

void debug_logf(const char* fmt, ...) {
    char buf[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    Serial.print(buf);
    events.send(buf, "log", millis());
}

// SPI batch buffer — raw bytes, formatted on flush
#define SPI_BATCH_MAX 256
static uint8_t spiBatchSend[SPI_BATCH_MAX];
static uint8_t spiBatchRecv[SPI_BATCH_MAX];
static int spiBatchLen = 0;

static const char HEX_CHARS[] = "0123456789ABCDEF";

void debug_spi(uint8_t sent, uint8_t recv) {
    if (spiBatchLen < SPI_BATCH_MAX) {
        spiBatchSend[spiBatchLen] = sent;
        spiBatchRecv[spiBatchLen] = recv;
        spiBatchLen++;
    }
    if (spiBatchLen >= SPI_BATCH_MAX) debug_spi_flush();
}

void debug_spi_flush() {
    if (spiBatchLen == 0) return;
    int len = spiBatchLen;
    spiBatchLen = 0;

    // Format: "XX:YY\n" per pair (6 chars each)
    static char buf[SPI_BATCH_MAX * 6 + 1];
    int pos = 0;
    for (int i = 0; i < len; i++) {
        buf[pos++] = HEX_CHARS[spiBatchSend[i] >> 4];
        buf[pos++] = HEX_CHARS[spiBatchSend[i] & 0xF];
        buf[pos++] = ':';
        buf[pos++] = HEX_CHARS[spiBatchRecv[i] >> 4];
        buf[pos++] = HEX_CHARS[spiBatchRecv[i] & 0xF];
        buf[pos++] = '\n';
    }
    buf[pos] = '\0';
    events.send(buf, "spi", millis());
}

// =============================================================================
// REST API Handlers
// =============================================================================

static void handleStatus(AsyncWebServerRequest* request) {
    char json[512];
    snprintf(json, sizeof(json),
        "{\"mode\":\"%s\",\"conn\":\"%s\",\"tc\":\"%s\",\"gen\":\"%s\","
        "\"tradePokemon\":%d,\"offerSlot\":%d,\"autoConfirm\":%s,"
        "\"opponentCount\":%d}",
        ctx->tradeMode == TRADE_MODE_CLONE ? "clone" : "storage",
        CONN_NAMES[ctx->connState],
        TC_NAMES[ctx->tcState],
        genName(ctx->gen),
        ctx->tradePokemon,
        ctx->offerSlot,
        ctx->autoConfirm ? "true" : "false",
        ctx->opponentCount);
    request->send(200, "application/json", json);
}

static void handleSetMode(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                           size_t index, size_t total) {
    String body = String((char*)data, len);
    TradeMode newMode;
    if (body.indexOf("\"storage\"") >= 0) {
        newMode = TRADE_MODE_STORAGE;
    } else {
        newMode = TRADE_MODE_CLONE;
    }
    ctx->tradeMode = newMode;
    storage_setTradeMode(newMode);
    request->send(200, "application/json", "{\"ok\":true}");
}

static void handleGetPokemon(AsyncWebServerRequest* request) {
    String genParam = request->pathArg(0);
    Generation g = (genParam == "gen1" || genParam == "1") ? GEN_1 : GEN_2;
    StoredPokemon* party = storage_getParty(g);

    String json = "[";
    for (int i = 0; i < PARTY_LENGTH; i++) {
        if (i > 0) json += ",";
        json += "{\"slot\":";
        json += i;
        json += ",\"occupied\":";
        json += party[i].occupied ? "true" : "false";

        if (party[i].occupied) {
            json += ",\"species\":";
            json += party[i].speciesIndex;
            json += ",\"speciesName\":\"";
            json += speciesName(g, party[i].speciesIndex);
            json += "\"";

            int level = 0;
            if (g == GEN_1) {
                Gen1PartyMon* mon = (Gen1PartyMon*)party[i].monData;
                level = mon->level;
            } else {
                Gen2PartyMon* mon = (Gen2PartyMon*)party[i].monData;
                level = mon->level;
            }
            json += ",\"level\":";
            json += level;

            char nick[NAME_LENGTH + 1];
            gbTextToAscii(party[i].nickname, nick, NAME_LENGTH);
            json += ",\"nickname\":\"";
            json += nick;
            json += "\"";
        }
        json += "}";
    }
    json += "]";
    request->send(200, "application/json", json);
}

static void handleDeletePokemon(AsyncWebServerRequest* request) {
    String genParam = request->pathArg(0);
    String slotParam = request->pathArg(1);
    Generation g = (genParam == "gen1" || genParam == "1") ? GEN_1 : GEN_2;
    int slot = slotParam.toInt();

    if (slot < 0 || slot >= PARTY_LENGTH) {
        request->send(400, "application/json", "{\"error\":\"invalid slot\"}");
        return;
    }

    storage_clearSlot(g, slot);
    request->send(200, "application/json", "{\"ok\":true}");
}

static void handleTradeOffer(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                              size_t index, size_t total) {
    String body = String((char*)data, len);
    int idx = body.indexOf("\"slot\":");
    if (idx >= 0) {
        int slot = body.substring(idx + 7).toInt();
        if (slot >= 0 && slot < PARTY_LENGTH) {
            ctx->offerSlot = slot;
        }
    }
    request->send(200, "application/json", "{\"ok\":true}");
}

static void handleTradeConfirm(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                                size_t index, size_t total) {
    ctx->confirmRequested = true;
    ctx->declineRequested = false;
    request->send(200, "application/json", "{\"ok\":true}");
}

static void handleTradeDecline(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                                size_t index, size_t total) {
    ctx->declineRequested = true;
    ctx->confirmRequested = false;
    request->send(200, "application/json", "{\"ok\":true}");
}

static void handleTradeAuto(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                             size_t index, size_t total) {
    String body = String((char*)data, len);
    ctx->autoConfirm = (body.indexOf("true") >= 0);
    request->send(200, "application/json", "{\"ok\":true}");
}

static void handleGetOpponent(AsyncWebServerRequest* request) {
    int count = ctx->opponentCount;
    int g = ctx->gen;
    String json = "[";
    for (int i = 0; i < count && i < PARTY_LENGTH; i++) {
        if (i > 0) json += ",";
        json += "{\"slot\":";
        json += i;
        json += ",\"species\":";
        json += ctx->opponentSpecies[i];
        json += ",\"speciesName\":\"";
        json += speciesName(g, ctx->opponentSpecies[i]);
        json += "\",\"level\":";
        json += ctx->opponentLevels[i];

        char nick[NAME_LENGTH + 1];
        gbTextToAscii((const uint8_t*)ctx->opponentNicknames[i], nick, NAME_LENGTH);
        json += ",\"nickname\":\"";
        json += nick;
        json += "\"}";
    }
    json += "]";
    request->send(200, "application/json", json);
}

// =============================================================================
// WiFi Init
// =============================================================================

void wifi_init(TradeContext* tradeCtx) {
    ctx = tradeCtx;

    // Start LittleFS
    if (!LittleFS.begin(true)) {
        Serial.println("[WIFI] LittleFS mount failed!");
    }

    // Configure WiFi AP
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_SSID, WIFI_PASSWORD);
    delay(100);

    IPAddress ip = WiFi.softAPIP();
    Serial.printf("[WIFI] AP started: SSID=%s IP=%s\n", WIFI_SSID, ip.toString().c_str());

    // SSE event source for debug page
    events.onConnect([](AsyncEventSourceClient* client) {
        client->send("connected", "log", millis());
    });
    server.addHandler(&events);

    // REST API routes (must be registered before serveStatic catch-all)
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/opponent", HTTP_GET, handleGetOpponent);

    server.on("^\\/api\\/pokemon\\/([a-z0-9]+)$", HTTP_GET, handleGetPokemon);
    server.on("^\\/api\\/pokemon\\/([a-z0-9]+)\\/([0-9]+)$", HTTP_DELETE, handleDeletePokemon);

    server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleSetMode);
    server.on("/api/trade/offer", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeOffer);
    server.on("/api/trade/confirm", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeConfirm);
    server.on("/api/trade/decline", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeDecline);
    server.on("/api/trade/auto", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeAuto);

    // Static files last (catch-all)
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    server.begin();
    Serial.println("[WIFI] Web server started on port 80");
}
