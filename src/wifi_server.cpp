#include "wifi_server.h"
#include "storage.h"
#include "trade_data.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <LittleFS.h>

// =============================================================================
// WiFi + Web Server Implementation
// =============================================================================

static AsyncWebServer server(80);
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
    // Parse {"mode":"clone"} or {"mode":"storage"}
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

    // Build JSON array
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

            // Extract level from mon data
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

            // Nickname
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
    // Parse {"slot":N}
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

        // Nickname
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

    // Serve static files from LittleFS
    server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html");

    // REST API routes
    server.on("/api/status", HTTP_GET, handleStatus);
    server.on("/api/opponent", HTTP_GET, handleGetOpponent);

    // Pokemon storage routes with path params
    server.on("^\\/api\\/pokemon\\/([a-z0-9]+)$", HTTP_GET, handleGetPokemon);
    server.on("^\\/api\\/pokemon\\/([a-z0-9]+)\\/([0-9]+)$", HTTP_DELETE, handleDeletePokemon);

    // POST routes need body handlers
    server.on("/api/mode", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleSetMode);
    server.on("/api/trade/offer", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeOffer);
    server.on("/api/trade/confirm", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeConfirm);
    server.on("/api/trade/decline", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeDecline);
    server.on("/api/trade/auto", HTTP_POST, [](AsyncWebServerRequest* r){}, nullptr, handleTradeAuto);

    server.begin();
    Serial.println("[WIFI] Web server started on port 80");
}
