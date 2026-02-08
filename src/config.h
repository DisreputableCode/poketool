#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include "soc/gpio_reg.h"

// =============================================================================
// GPIO Pin Definitions (ESP32-C3 Super Mini)
// =============================================================================
#define PIN_MOSI  5   // ESP32 -> Game Boy (SI on GB side)
#define PIN_MISO  6   // Game Boy -> ESP32 (SO on GB side)
#define PIN_SCLK  7   // Clock from Game Boy
#define PIN_LED   8   // Built-in LED

// Fast GPIO macros using direct register access
#define READ_GPIO(pin)        ((REG_READ(GPIO_IN_REG) >> (pin)) & 1)
#define WRITE_GPIO_HIGH(pin)  REG_WRITE(GPIO_OUT_W1TS_REG, 1 << (pin))
#define WRITE_GPIO_LOW(pin)   REG_WRITE(GPIO_OUT_W1TC_REG, 1 << (pin))

// =============================================================================
// Timing Constants
// =============================================================================
#define IDLE_TIMEOUT_MS       1000    // No clock activity = session over
#define BYTE_DELAY_US         100     // Delay between bytes
#define CLOCK_TIMEOUT_US      500000  // 500ms timeout waiting for a clock edge

// =============================================================================
// Link Cable Protocol Constants
// =============================================================================
#define PKMN_BLANK            0x00
#define PKMN_MASTER           0x01
#define PKMN_SLAVE            0x02
#define PKMN_CONNECTED        0x60  // Gen 1 connection byte
#define PKMN_CONNECTED_GEN2   0x61  // Gen 2 connection byte
#define PKMN_WAIT             0x7F

#define PKMN_ACTION           0x60

// Menu highlight bytes
#define ITEM_1_HIGHLIGHTED    0xD0
#define ITEM_2_HIGHLIGHTED    0xD1
#define ITEM_3_HIGHLIGHTED    0xD2

// Menu selection bytes
#define TRADE_CENTRE          0xD4  // Item 1 selected
#define COLOSSEUM             0xD5  // Item 2 selected
#define BREAK_LINK            0xD6  // Item 3 selected

// Serial protocol bytes
#define SERIAL_PREAMBLE_BYTE  0xFD
#define SERIAL_NO_DATA_BYTE   0xFE
#define SERIAL_PATCH_TERM     0xFF  // Patch list part terminator

// Trade selection: 0x60 + pokemon_index (0-5)
#define TRADE_POKEMON_BASE    0x60

// =============================================================================
// Data Structure Sizes
// =============================================================================
#define NAME_LENGTH           11
#define PARTY_LENGTH          6
#define NUM_MOVES             4

// Gen 1 (from pokered wram.asm)
#define GEN1_BOX_STRUCT_SIZE     33   // 0x21
#define GEN1_PARTY_STRUCT_SIZE   44   // 0x2C
#define GEN1_PREAMBLE_SIZE       6
#define GEN1_RANDOM_BLOCK_SIZE   17   // 7 preamble + 10 random
#define GEN1_PARTY_BLOCK_SIZE    424  // 6+11+8+(44+22)*6+3
#define GEN1_PATCH_LIST_SIZE     200

// Gen 2 (from pokecrystal constants/pokemon_data_constants.asm)
#define GEN2_BOX_STRUCT_SIZE     32   // 0x20
#define GEN2_PARTY_STRUCT_SIZE   48   // 0x30
#define GEN2_PREAMBLE_SIZE       6
#define GEN2_RANDOM_BLOCK_SIZE   17
#define GEN2_PARTY_BLOCK_SIZE    450  // 6+11+8+2+(48+22)*6+3
#define GEN2_PATCH_LIST_SIZE     200

// Patch list split point (SERIAL_PATCH_DATA_SIZE from ROM)
#define PATCH_DATA_SPLIT         252  // 0xFC

// =============================================================================
// Printer Protocol Constants
// =============================================================================
#define GBP_SYNC_0            0x88
#define GBP_SYNC_1            0x33
#define GBP_CMD_INIT          0x01
#define GBP_CMD_PRINT         0x02
#define GBP_CMD_DATA          0x04
#define GBP_CMD_BREAK         0x08
#define GBP_CMD_INQUIRY       0x0F
#define GBP_DEVICE_ID         0x81

// Printer status bits
#define GBP_STATUS_CHECKSUM   0x01
#define GBP_STATUS_BUSY       0x02
#define GBP_STATUS_FULL       0x04
#define GBP_STATUS_UNPROC     0x08
#define GBP_STATUS_JAM        0x20
#define GBP_STATUS_ERROR      0x40
#define GBP_STATUS_LOWBAT     0x80

// Printer data sizes
#define GBP_DATA_PACKET_SIZE  640   // Tile data per DATA command
#define GBP_TILE_SIZE         16    // Bytes per 8x8 tile (2bpp)
#define GBP_TILES_PER_ROW     20    // 160px / 8px per tile
#define GBP_MAX_IMAGE_SIZE    8192  // Max tile data per image

// =============================================================================
// Storage Constants
// =============================================================================
#define MAX_STORED_POKEMON    6
#define MAX_PRINTER_IMAGES    5     // FIFO depth for printed images

// =============================================================================
// WiFi Configuration
// =============================================================================
#define WIFI_SSID             "PokeTool"
#define WIFI_PASSWORD         "poketool"

// =============================================================================
// Application State
// =============================================================================
enum AppState {
    STATE_IDLE,
    STATE_TRADE,
    STATE_PRINTER
};

enum TradeMode {
    TRADE_MODE_CLONE,
    TRADE_MODE_STORAGE
};

enum Generation {
    GEN_UNKNOWN,
    GEN_1,
    GEN_2
};

#endif // CONFIG_H
