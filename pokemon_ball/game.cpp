/*
 * game.cpp — Central state machine.
 *
 * Flow:
 *   IDLE     → pokeball, continuous inference + score publishing, poll for shake
 *   SENSING  → pokeball "TOUCH!", continuous inference + score publishing,
 *              wait for touch (no timeout — keep sensing until user taps)
 *   REVEALED → pokemon (or Sanjini), auto-return to IDLE after 5 s
 *
 * On touch: grab current best non-idle gesture → reveal pokemon.
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
static unsigned long lastShakePoll = 0;
static unsigned long lastScorePub  = 0;

static GestureResult pendingGesture;

#define SCORE_PUB_MS 250

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
    gesturePoll();
    unsigned long now = millis();

    // Publish real-time scores in IDLE and SENSING
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
                Serial.println(F("[GAME] shake! sensing + waiting touch..."));
                displayPokeball(true);   // "TOUCH!" hint
                enterState(State::SENSING);
            }
        }
        break;
    }

    // -------------------------------------------------- SENSING
    // Keep sensing + publishing scores until touch.
    // No timeout — user can shake as long as they want.
    case State::SENSING: {
        if (touchTapped()) {
            Serial.println(F("[GAME] touch! revealing..."));
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
        if (now - stateEnterMs >= 3000 && touchTapped()) {
            displayPokeball(false);
            enterState(State::IDLE);
        }
        break;
    }
    } // switch
}
