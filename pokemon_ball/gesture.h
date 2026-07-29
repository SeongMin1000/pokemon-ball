/*
 * gesture.h — Gesture inference module (rolling window, bee2 pattern).
 *
 * gesturePoll() must be called every loop iteration — it samples the
 * BNO055 at 100 Hz into a ring buffer and runs the Edge Impulse
 * classifier every 250 ms.  Results (scores for all labels) are
 * available immediately via gestureGetScores() / gestureGetResult().
 */
#pragma once
#include <Arduino.h>

#define MAX_LABELS 6

struct GestureResult {
    const char* label;      // winning label (or "idle" / "uncertain")
    float       confidence; // 0.0 – 1.0
    bool        valid;      // false if idle / below threshold
};

bool  gestureBegin();
void  gesturePostWifiInit();

// Call every loop iteration — non-blocking 100 Hz sampling + 250 ms inference.
void  gesturePoll();

// Quick non-blocking shake check (linear-accel magnitude).
bool  gesturePollShake();

// Latest inference result (best label).
GestureResult gestureGetResult();

// Fill outScores with the latest per-label scores (0.0–1.0).
// Returns the number of labels written.
int   gestureGetScores(float* outScores, int maxLabels);

// Label name by index (0 .. gestureGetLabelCount()-1).
const char* gestureGetLabel(int index);
int   gestureGetLabelCount();
