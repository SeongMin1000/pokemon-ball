/*
 * gesture.h — Gesture inference module (BNO055 + Edge Impulse).
 *
 * Two public operations:
 *   gestureBegin()   — initialise I2C and BNO055
 *   gesturePollShake() — non-blocking: returns true when the board is shaken
 *   gestureInfer()   — blocking: collects 2 s of data and runs the classifier,
 *                       returns the winning label + confidence.
 *
 * When USE_EDGE_IMPULSE is defined in config.h the real Edge Impulse model
 * is used.  Otherwise a stub analyses the collected data and returns a
 * label taken from POKEMON_TABLE so the rest of the program is testable.
 */
#pragma once
#include <Arduino.h>

struct GestureResult {
    const char* label;      // winning label (matches a POKEMON_TABLE entry)
    float       confidence; // 0.0 – 1.0
    bool        valid;      // false if idle / below threshold / hardware error
};

bool  gestureBegin();

// Re-apply NDOF mode after WiFi is up (BNO055 brownout workaround).
void  gesturePostWifiInit();

// Quick non-blocking check (call from the idle loop).
bool  gesturePollShake();

// Blocking 2-second collection + inference.
GestureResult gestureInfer();
