/*
 * config.h — Central configuration for the Pokemon Ball project.
 *
 * All tunable values (pins, WiFi, MQTT, gesture/pokemon mapping, hidden
 * character) live here so that other modules stay free of hard-coded data.
 * Add or edit entries in POKEMON_TABLE to introduce new gestures / pokemon.
 */
#pragma once
#include <Arduino.h>

// ========================================================================
// Image assets (PROGMEM JPEG C arrays)
// ========================================================================
#include "images/pokemon_ball.h"
#include "images/pikachu.h"
#include "images/charmander.h"
#include "images/squirtle.h"
#include "images/ditto.h"
#include "images/sanjini.h"

#define POKEBALL_IMAGE      pokemon_ball
#define POKEBALL_IMAGE_SIZE sizeof(pokemon_ball)

// Hidden character image.
#define HIDDEN_IMAGE        sanjini
#define HIDDEN_IMAGE_SIZE   sizeof(sanjini)

// ========================================================================
// Edge Impulse switch
// ========================================================================
// Uncomment this line AFTER downloading the Edge Impulse C++ export and
// placing gesture_inferencing.h in the sketch folder (or library path).
// While it stays commented, gesture.cpp uses a built-in stub that returns
// mock labels so the full pipeline can be exercised without the model.
//
#define USE_EDGE_IMPULSE

// ========================================================================
// Hardware pins  (XIAO ESP32S3 + Seeed Round Display + GY-BNO055)
// ========================================================================
// TFT pins are set in TFT_eSPI User_Setup (Setup66) — do NOT redefine here.
//   TFT_CS=D1(GPIO2)  TFT_DC=D3(GPIO4)  TFT_BL=D6(GPIO43)
//   TFT_SCLK=D8(GPIO7) TFT_MOSI=D10(GPIO9) TFT_MISO=D9(GPIO8)

#define I2C_SDA     5        // D4 — shared with BNO055
#define I2C_SCL     6        // D5
#define BNO055_ADDR 0x29

#define TOUCH_PIN   T1       // GPIO1 (D0) — does not clash with TFT or I2C
#define TOUCH_THRESHOLD 40000  // S3: value RISES above this when touched

#define LED_PIN     21       // user LED (inverted: LOW = ON)
#define TFT_BL_PIN  43       // backlight, same as TFT_BL in Setup66

// ========================================================================
// Display geometry (GC9A01 240×240 round)
// ========================================================================
#define SCREEN_W    240
#define SCREEN_H    240
#define CENTER_X    (SCREEN_W / 2)
#define CENTER_Y    (SCREEN_H / 2)

// ========================================================================
// WiFi  (STA mode — AP mode is intentionally NOT used)
// ========================================================================
#define WIFI_SSID   "seongmin"
#define WIFI_PASS   "35793579"

// ========================================================================
// MQTT
// ========================================================================
#define MQTT_HOST   "10.61.58.194"
#define MQTT_PORT   1883
#define MQTT_CLIENT_ID "pokemon_ball"

#define TOPIC_CONFIG   "pokemon/config"
#define TOPIC_PREDICT  "pokemon/predict"
#define TOPIC_GESTURE  "pokemon/gesture"
#define TOPIC_CAPTURE  "pokemon/capture"

// ========================================================================
// Shake / inference tuning
// ========================================================================
#define SHAKE_CHECK_MS    50     // how often to poll for shake in IDLE
#define SHAKE_THRESHOLD   12.0f  // linear-accel magnitude (m/s²) that = "shaking"
#define INFERENCE_SAMPLE_MS 10   // 100 Hz (must match EI model frequency)
#define INFERENCE_SAMPLES   200  // 2 seconds at 100 Hz (overridden by EI model)
#define MIN_CONFIDENCE      0.50f // below this the gesture is discarded

// Gestures whose label means "no gesture" — inference result is ignored.
// Edit to match whatever the trained model uses for the resting class.
const char* const IDLE_LABELS[] = { "idle", "IDLE", "Idle" };
const int IDLE_LABEL_COUNT = 3;

// ========================================================================
// Hidden character  (부산대학교 "산지니")
// ========================================================================
#define HIDDEN_NAME        "Sanjini"
#define HIDDEN_PROBABILITY 2      // percent chance on every reveal (0-100)

// ========================================================================
// Gesture → Pokemon mapping table
// --------------------------------------------------------------------
// Each row maps one Edge Impulse gesture label to a pokemon.
// To add / change a gesture or pokemon, simply edit this table.
//
//   gestureLabel  — must EXACTLY match the label string produced by the
//                   Edge Impulse model (result.classification[i].label).
//   pokemonName   — human-readable name, also published via MQTT.
//   jpgImage      — PROGMEM JPEG byte array (nullptr → coloured placeholder
//                   is drawn instead).  Add #include "your_image.h" above
//                   and reference the array name here.
//   jpgSize       — sizeof(the array).  Use 0 when jpgImage is nullptr.
//   placeholderColor — 16-bit RGB565 colour used for the placeholder circle.
// ========================================================================

// Placeholder colours as raw RGB565 (keeps config.h free of any display
// library dependency).  TFT_eSPI uses the same encoding so they pass
// straight through to tft.fillCircle() etc.
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_YELLOW  0xFFE0
#define COLOR_CYAN    0x07FF
#define COLOR_MAGENTA 0xF81F
#define COLOR_ORANGE  0xFD20
#define COLOR_PURPLE  0x780F

struct PokemonEntry {
    const char*      gestureLabel;
    const char*      pokemonName;
    const uint8_t*   jpgImage;
    uint32_t         jpgSize;
    uint16_t         placeholderColor;
};

// TODO: Replace gesture labels with real Edge Impulse model labels once
//       trained.  Add/remove rows freely — GESTURE_COUNT is derived.
const PokemonEntry POKEMON_TABLE[] = {
    { "left",   "Pikachu",    pikachu,     sizeof(pikachu),     COLOR_RED    },
    { "right",  "Charmander", charmander,  sizeof(charmander),  COLOR_GREEN  },
    { "up",     "Squirtle",   squirtle,    sizeof(squirtle),    COLOR_BLUE   },
    { "down",   "Ditto",      ditto,       sizeof(ditto),       COLOR_PURPLE },
    { "circle", "Pokemon_E",  nullptr,     0,                   COLOR_YELLOW },
};

const int GESTURE_COUNT = sizeof(POKEMON_TABLE) / sizeof(POKEMON_TABLE[0]);

// ========================================================================
// Timing
// ========================================================================
#define REVEAL_DISPLAY_MS 5000   // how long a caught pokemon stays on screen
#define DEBOUNCE_MS       200    // touch debounce
