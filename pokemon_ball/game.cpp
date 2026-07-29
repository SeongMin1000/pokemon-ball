/*
 * game.cpp — Central state machine that wires the modules together.
 *
 * Uses continuous rolling-window inference (gesturePoll) so the web
 * dashboard gets real-time score updates during sensing.
 *
 * States:
 *   IDLE          → pokeball, continuous inference + score publishing
 *   SENSING       → "Sensing..." shown, wait for valid classification
 *   GESTURE_READY → pokeball "TOUCH!", wait for tap
 *   REVEALED      → pokemon (or Sanjini), auto-return to IDLE
 */
#include "game.h"
#include "config.h"
#include "display.h"
#include "gesture.h"
#include "touch.h"
#include "mqtt_client.h"

enum class State {
    IDLE,
    SENSING,
    GESTURE_READY,
    REVEALED,
};

static State         state         = State::IDLE;
static unsigned long stateEnterMs  = 0;
static unsigned long lastShakePoll = 0;
static unsigned long lastScorePub  = 0;

static GestureResult pendingGesture;

#define SCORE_PUB_MS 250   // publish scores every 250 ms
#define SENSING_TIMEOUT_MS 3000   // give up if no valid gesture in 3 s

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------

static void enterState(State s) {
    state        = s;
    stateEnterMs = millis();
}

static int findGestureIndex(const char* label) {
    if (!label) return -1;
    for (int i = 0; i < GESTURE_COUNT; i++) {
        if (strcmp(label, POKEMON_TABLE[i].gestureLabel) == 0)
            return i;
    }
    return -1;
}

static bool rollHidden() {
    return (random(1, 101) <= HIDDEN_PROBABILITY);
}

static void publishScores() {
    float scores[MAX_LABELS];
    int n = gestureGetScores(scores, MAX_LABELS);
    const char* labels[MAX_LABELS];
    for (int i = 0; i < n; i++) labels[i] = gestureGetLabel(i);
    mqttPublishScores(labels, scores, n);
}

static void reveal() {
    bool confirmedHidden = rollHidden();

    if (confirmedHidden) {
        displayHidden(HIDDEN_NAME, HIDDEN_IMAGE, HIDDEN_IMAGE_SIZE);
        mqttPublishResult(pendingGesture.label, HIDDEN_NAME, true);
        Serial.printf("[GAME] HIDDEN! %s appears!\n", HIDDEN_NAME);
    } else {
        int idx = findGestureIndex(pendingGesture.label);
        if (idx < 0) idx = 0;
        const PokemonEntry& pe = POKEMON_TABLE[idx];
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
    // Continuous sampling + inference (must be called every iteration)
    gesturePoll();

    unsigned long now = millis();

    // Publish real-time scores in IDLE and SENSING states
    if ((state == State::IDLE || state == State::SENSING) &&
        now - lastScorePub >= SCORE_PUB_MS) {
        lastScorePub = now;
        publishScores();
    }

    switch (state) {

    // -------------------------------------------------- IDLE
    case State::IDLE: {
        if (now - lastShakePoll >= SHAKE_CHECK_MS) {
            lastShakePoll = now;
            if (gesturePollShake()) {
                Serial.println(F("[GAME] shake detected — sensing..."));
                displayMessage("Sensing...", nullptr);
                enterState(State::SENSING);
            }
        }
        break;
    }

    // -------------------------------------------------- SENSING
    case State::SENSING: {
        GestureResult gr = gestureGetResult();
        if (gr.valid) {
            pendingGesture = gr;
            mqttPublishGesture(gr.label);
            displayPokeball(true);
            enterState(State::GESTURE_READY);
        } else if (now - stateEnterMs >= SENSING_TIMEOUT_MS) {
            // No valid gesture within 3 s → back to idle
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
        if (now - stateEnterMs >= REVEAL_DISPLAY_MS) {
            displayPokeball(false);
            enterState(State::IDLE);
        }
        if (now - stateEnterMs >= 3000 && touchTapped()) {
            displayPokeball(false);
            enterState(State::IDLE);
        }
        break;
    }
    } // switch
}
