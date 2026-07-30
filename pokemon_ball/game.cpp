/*
 * game.cpp — Central state machine.
 *
 * Flow:
 *   IDLE     → pokeball, continuous inference + score publishing
 *   SENSING  → pokeball "TOUCH!", auto-reveal after 2s sensing window
 *   REVEALED → pokemon (or Sanjini), auto-return to IDLE after 5 s
 *
 * Touch: IDLE → SENSING (tap to start), SENSING → REVEALED (tap to skip wait).
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
    REVEALED,
};

static State         state         = State::IDLE;
static unsigned long stateEnterMs  = 0;
static unsigned long lastScorePub  = 0;

static GestureResult pendingGesture;

#define SCORE_PUB_MS 500   // publish scores every 500 ms (match inference rate)

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

static bool isIdleLabel(const char* label) {
    if (!label) return true;
    for (int j = 0; j < IDLE_LABEL_COUNT; j++)
        if (strcmp(label, IDLE_LABELS[j]) == 0) return true;
    return false;
}

static bool rollHidden() {
    return (random(1, 101) <= HIDDEN_PROBABILITY);
}

static void publishScores() {
    float scores[MAX_LABELS];
    int n = gestureGetScores(scores, MAX_LABELS);
    const char* labels[MAX_LABELS];
    for (int i = 0; i < n; i++) labels[i] = gestureGetLabel(i);
    mqttPublishPredict(labels, scores, n);
}

// Find the best non-idle label from the latest scores.
static const char* getBestNonIdleLabel() {
    float scores[MAX_LABELS];
    int n = gestureGetScores(scores, MAX_LABELS);
    float best = -1.0f;
    const char* bestLabel = nullptr;
    for (int i = 0; i < n; i++) {
        const char* lbl = gestureGetLabel(i);
        if (isIdleLabel(lbl)) continue;
        if (scores[i] > best) { best = scores[i]; bestLabel = lbl; }
    }
    return bestLabel;
}

static void reveal() {
    // Determine which gesture to use
    const char* label = pendingGesture.label;
    if (!label || isIdleLabel(label)) {
        label = getBestNonIdleLabel();
    }
    if (!label) label = POKEMON_TABLE[0].gestureLabel;
    pendingGesture.label = label;

    bool confirmedHidden = rollHidden();

    if (confirmedHidden) {
        displayHidden(HIDDEN_NAME, HIDDEN_IMAGE, HIDDEN_IMAGE_SIZE);
        mqttPublishCapture(label, HIDDEN_NAME, true);
        Serial.printf("[GAME] HIDDEN! %s appears!\n", HIDDEN_NAME);
    } else {
        int idx = findGestureIndex(label);
        if (idx < 0) idx = 0;
        const PokemonEntry& pe = POKEMON_TABLE[idx];
        displayPokemon(pe.pokemonName, pe.jpgImage, pe.jpgSize,
                       pe.placeholderColor);
        mqttPublishCapture(pe.gestureLabel, pe.pokemonName, false);
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
    // 1. Sample IMU (fast, non-blocking)
    gesturePoll();

    unsigned long now = millis();

    // 2. Check touch FIRST (before any blocking inference/MQTT)
    if (state == State::SENSING && touchTapped()) {
        Serial.println(F("[GAME] touch! revealing..."));
        pendingGesture = gestureGetResult();
        reveal();
        enterState(State::REVEALED);
        return;   // skip everything else this iteration
    }
    if (state == State::REVEALED && now - stateEnterMs >= 3000 && touchTapped()) {
        displayPokeball(false);
        enterState(State::IDLE);
        return;
    }

    // 3. Run inference (blocking ~50ms, only if no touch)
    gesturePollInfer();

    // 4. Publish real-time scores in IDLE and SENSING
    if ((state == State::IDLE || state == State::SENSING) &&
        now - lastScorePub >= SCORE_PUB_MS) {
        lastScorePub = now;
        publishScores();
    }

    switch (state) {

    // -------------------------------------------------- IDLE
    case State::IDLE: {
        if (touchTapped()) {
            Serial.println(F("[GAME] tap! sensing..."));
            displayPokeball(true);
            enterState(State::SENSING);
        }
        break;
    }

    // -------------------------------------------------- SENSING
    case State::SENSING: {
        // Auto-reveal after sensing window
        if (now - stateEnterMs >= 2000) {
            pendingGesture = gestureGetResult();
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
        break;
    }
    } // switch
}
