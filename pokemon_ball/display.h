/*
 * display.h — Round LCD (GC9A01) display module.
 *
 * Provides a small set of high-level screen functions used by the game
 * state machine.  All TFT_eSPI and JPEGDecoder details are hidden in
 * display.cpp so that the rest of the program never touches the display
 * library directly.
 */
#pragma once
#include <Arduino.h>

// Screen the game can show.
enum class Screen {
    POKEBALL_IDLE,     // default pokeball (waiting for shake)
    POKEBALL_READY,    // pokeball with "throw!" hint (gesture inferred)
    POKEMON_PLACEHOLDER, // coloured circle + pokemon name (no JPEG)
    POKEMON_JPEG,      // JPEG image of a pokemon
    HIDDEN_PLACEHOLDER,// coloured circle + hidden char name
    HIDDEN_JPEG,       // JPEG image of the hidden char
};

bool displayBegin();

void displayPokeball(bool ready);
void displayPokemon(const char* name, const uint8_t* jpg, uint32_t jpgSize,
                    uint16_t placeholderColor);
void displayHidden(const char* name, const uint8_t* jpg, uint32_t jpgSize);
void displayMessage(const char* line1, const char* line2);
