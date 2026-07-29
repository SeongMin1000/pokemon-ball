/*
 * game.cpp — Central state machine that wires the modules together.
 *
 * The hidden-character (Sanjini) logic lives here: on every reveal there is
 * a HIDDEN_PROBABILITY percent chance that the gesture result is overridden
 * and the hidden character is shown instead.  The MQTT result message
 * carries the "hidden" flag accordingly.
 */
#include "game.h"
#include "config.h"
#include "display.h"
#include "gesture.h"
#include "touch.h"
#include "mqtt_client.h"

enum class State {
    IDLE,
    INFERING,
    GESTURE_READY,
    REVEALED,
};

static State         state         = State::IDLE;
static unsigned long stateEnterMs  = 0;
static unsigned long lastShakePoll = 0;

// The gesture inferred during INFERING — not yet "confirmed" until the
// user taps the touch sensor.
static GestureResult pendingGesture;

// Index into POKEMON_TABLE for the confirmed gesture (or -1 for hidden).
static int  confirmedIndex  = -1;
static bool confirmedHidden = false;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void enterState(State s) {
    state        = s;
    stateEnterMs = millis();
}

// Look up a gesture label in the table; returns -1 if not found.
static int findGestureIndex(const char* label) {
    if (!label) return -1;
    for (int i = 0; i < GESTURE_COUNT; i++) {
        if (strcmp(label, POKEMON_TABLE[i].gestureLabel) == 0)
            return i;
    }
    return -1;
}

// Roll the hidden-character dice.
static bool rollHidden() {
    return (random(1, 101) <= HIDDEN_PROBABILITY);  // 1..100 inclusive
}

// Reveal the pokemon (or hidden character) and publish the result.
static void reveal() {
    confirmedHidden = rollHidden();

    if (confirmedHidden) {
        displayHidden(HIDDEN_NAME, nullptr, 0);
        mqttPublishResult(pendingGesture.label, HIDDEN_NAME, true);
        Serial.printf("[GAME] HIDDEN! %s appears!\n", HIDDEN_NAME);
    } else {
        confirmedIndex = findGestureIndex(pendingGesture.label);
        if (confirmedIndex < 0) {
            // Label not in table — fall back to entry 0 as a safety net.
            confirmedIndex = 0;
        }
        const PokemonEntry& pe = POKEMON_TABLE[confirmedIndex];
        displayPokemon(pe.pokemonName, pe.jpgImage, pe.jpgSize,
                       pe.placeholderColor);
        mqttPublishResult(pe.gestureLabel, pe.pokemonName, false);
        Serial.printf("[GAME] %s → %s\n", pe.gestureLabel, pe.pokemonName);
    }
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

void gameInit() {
    displayPokeball(false);
    enterState(State::IDLE);
}

void gameLoop() {
    unsigned long now = millis();

    switch (state) {

    // -------------------------------------------------- IDLE
    case State::IDLE: {
        // Poll for shake at the configured interval (non-blocking).
        if (now - lastShakePoll >= SHAKE_CHECK_MS) {
            lastShakePoll = now;
            if (gesturePollShake()) {
                Serial.println(F("[GAME] shake detected — inferring..."));
                displayMessage("Sensing...", nullptr);
                enterState(State::INFERING);
            }
        }
        break;
    }

    // -------------------------------------------------- INFERING
    case State::INFERING: {
        // Blocking 2-second collection + classification.
        pendingGesture = gestureInfer();

        if (pendingGesture.valid) {
            mqttPublishGesture(pendingGesture.label);
            displayPokeball(true);   // "TOUCH!" hint
            enterState(State::GESTURE_READY);
        } else {
            // Idle / low-confidence → back to the plain pokeball.
            displayPokeball(false);
            enterState(State::IDLE);
        }
        break;
    }

    // -------------------------------------------------- GESTURE_READY
    case State::GESTURE_READY: {
        if (touchTapped()) {
            Serial.println(F("[GAME] touch! confirming..."));
            reveal();
            enterState(State::REVEALED);
        }
        break;
    }

    // -------------------------------------------------- REVEALED
    case State::REVEALED: {
        // Auto-return to idle after the reveal duration.
        if (now - stateEnterMs >= REVEAL_DISPLAY_MS) {
            displayPokeball(false);
            enterState(State::IDLE);
        }
        // Allow an early tap to skip back to idle immediately.
        if (touchTapped()) {
            displayPokeball(false);
            enterState(State::IDLE);
        }
        break;
    }
    } // switch
}
