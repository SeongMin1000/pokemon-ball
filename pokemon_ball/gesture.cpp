/*
 * gesture.cpp — BNO055 rolling-window inference (bee2 pattern).
 *
 * Continuously samples 6-axis IMU data at 100 Hz into a ring buffer
 * (200 samples × 6 axes = 2 s window).  Runs run_classifier() every
 * 250 ms on the trailing window.  All scores are available immediately
 * via gestureGetScores().
 *
 * When USE_EDGE_IMPULSE is not defined, a stub generates pseudo-scores
 * from the real accelerometer data so the full pipeline is testable.
 */
#include "gesture.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#ifdef USE_EDGE_IMPULSE
#include "pokemon_gesture_inferencing.h"
#endif

static Adafruit_BNO055 bno = Adafruit_BNO055(55, BNO055_ADDR, &Wire);
static bool bnoOK = false;

// ---- Ring buffer -------------------------------------------------------
#ifdef USE_EDGE_IMPULSE
#define N_SAMP EI_CLASSIFIER_RAW_SAMPLE_COUNT
#define N_AX   EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME
#define FRAME  EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE
#else
#define N_SAMP 200
#define N_AX   6
#define FRAME  (N_SAMP * N_AX)
#endif

static float ring[N_SAMP][N_AX];
static int   head  = 0;
static bool  filled = false;

// ---- Inference timing -------------------------------------------------
static uint32_t nextSample = 0;   // micros() for 100 Hz sampling
static uint32_t nextInfer  = 0;   // millis() for 250 ms inference

// ---- Latest results ---------------------------------------------------
static float    latestScores[MAX_LABELS] = {0};
static const char* latestTopLabel = "...";
static float    latestTopConf     = 0.0f;
static bool     latestValid       = false;

#ifdef USE_EDGE_IMPULSE
// EI signal callback — copy ring buffer oldest-first.
static int getData(size_t offset, size_t length, float* out) {
    for (size_t i = 0; i < length; i++) {
        size_t f   = offset + i;
        size_t s   = f / N_AX;
        size_t a   = f % N_AX;
        size_t idx = filled ? (head + s) % N_SAMP : s;
        out[i] = ring[idx][a];
    }
    return 0;
}
#endif

// ---- Label helpers -----------------------------------------------------
static const char* STUB_LABELS[] = { "circle", "down", "idle", "left", "right", "up" };
static const int   STUB_COUNT = 6;

int gestureGetLabelCount() {
#ifdef USE_EDGE_IMPULSE
    return EI_CLASSIFIER_LABEL_COUNT;
#else
    return STUB_COUNT;
#endif
}

const char* gestureGetLabel(int index) {
#ifdef USE_EDGE_IMPULSE
    if (index < 0 || index >= (int)EI_CLASSIFIER_LABEL_COUNT) return "?";
    return ei_classifier_inferencing_categories[index];
#else
    if (index < 0 || index >= STUB_COUNT) return "?";
    return STUB_LABELS[index];
#endif
}

// ---- Public API --------------------------------------------------------

bool gestureBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);
    Wire.setTimeOut(2000);   // 2ms I2C timeout — prevents hang with WiFi active

    if (bno.begin()) {
        bno.setExtCrystalUse(true);
        bnoOK = true;
        Serial.println(F("[GESTURE] BNO055 OK"));
    } else {
        Serial.println(F("[GESTURE] BNO055 FAIL"));
    }
    nextSample = micros();
    nextInfer  = millis() + 1000;   // first inference after 1 s of data
    return bnoOK;
}

void gesturePostWifiInit() {
    // no-op (bee2 proved getVector works after WiFi STA)
}

static void runInferenceInternal();   // forward declaration

void gesturePoll() {
    if (!bnoOK) return;
    uint32_t now = micros();

    // ---- 100 Hz sampling into ring buffer (fast, non-blocking) ----
    if ((int32_t)(now - nextSample) >= 0) {
        nextSample = now + 10000;   // 10 ms → 100 Hz
        imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
        imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);
        ring[head][0] = a.x();
        ring[head][1] = a.y();
        ring[head][2] = a.z();
        ring[head][3] = g.x();
        ring[head][4] = g.y();
        ring[head][5] = g.z();
        head = (head + 1) % N_SAMP;
        if (head == 0) filled = true;
    }
}

void gesturePollInfer() {
    if (!bnoOK) return;
    // ---- 500 ms inference (blocking ~50ms, call AFTER touch check) ----
    if ((int32_t)(millis() - nextInfer) >= 0 && (filled || head >= N_SAMP / 2)) {
        nextInfer = millis() + 500;
        runInferenceInternal();
    }
}

static void runInferenceInternal() {
#ifdef USE_EDGE_IMPULSE
    ei::signal_t signal;
    signal.total_length = FRAME;
    signal.get_data     = &getData;

    ei_impulse_result_t result;
    if (run_classifier(&signal, &result, false) != EI_IMPULSE_OK) return;

    // Collect all scores + find best
    float bestVal = -1.0f;
    const char* bestLabel = "...";
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        latestScores[i] = result.classification[i].value;
        if (result.classification[i].value > bestVal) {
            bestVal   = result.classification[i].value;
            bestLabel = ei_classifier_inferencing_categories[i];
        }
    }
    latestTopLabel = bestLabel;
    latestTopConf  = bestVal;
    latestValid    = (bestVal >= MIN_CONFIDENCE);

#else
    // ---- Stub: derive pseudo-scores from ring buffer stats ----
    float meanA[3] = {0}, varA[3] = {0};
    int n = filled ? N_SAMP : head;
    if (n < 10) return;

    for (int i = 0; i < n; i++) {
        int idx = filled ? (head + i) % N_SAMP : i;
        for (int a = 0; a < 3; a++) meanA[a] += ring[idx][a];
    }
    for (int a = 0; a < 3; a++) meanA[a] /= n;
    for (int i = 0; i < n; i++) {
        int idx = filled ? (head + i) % N_SAMP : i;
        for (int a = 0; a < 3; a++) {
            float d = ring[idx][a] - meanA[a];
            varA[a] += d * d;
        }
    }
    for (int a = 0; a < 3; a++) varA[a] /= n;

    // Map variance to label scores (stub labels: circle, down, idle, left, right, up)
    float totalVar = varA[0] + varA[1] + varA[2] + 1.0f;
    float idleScore = 1.0f / (1.0f + totalVar * 0.5f);
    float activeScore = 1.0f - idleScore;

    // Distribute active score based on dominant axis
    float xRatio = varA[0] / totalVar;
    float yRatio = varA[1] / totalVar;
    float zRatio = varA[2] / totalVar;

    latestScores[0] = activeScore * zRatio * 0.8f;              // circle
    latestScores[1] = activeScore * yRatio * 0.5f;              // down
    latestScores[2] = idleScore;                                // idle
    latestScores[3] = activeScore * xRatio * (meanA[0] < 0 ? 0.8f : 0.3f); // left
    latestScores[4] = activeScore * xRatio * (meanA[0] >= 0 ? 0.8f : 0.3f); // right
    latestScores[5] = activeScore * yRatio * 0.5f;              // up

    // Normalize
    float sum = 0;
    for (int i = 0; i < STUB_COUNT; i++) sum += latestScores[i];
    if (sum > 0) for (int i = 0; i < STUB_COUNT; i++) latestScores[i] /= sum;

    float bestVal = -1; int bestIdx = 2;
    for (int i = 0; i < STUB_COUNT; i++) {
        if (latestScores[i] > bestVal) { bestVal = latestScores[i]; bestIdx = i; }
    }
    latestTopLabel = STUB_LABELS[bestIdx];
    latestTopConf  = bestVal;
    latestValid    = (bestIdx != 2) && (bestVal >= MIN_CONFIDENCE);  // not idle
#endif

    // Discard idle labels
    if (latestValid && latestTopLabel) {
        for (int j = 0; j < IDLE_LABEL_COUNT; j++) {
            if (strcmp(latestTopLabel, IDLE_LABELS[j]) == 0) {
                latestValid = false;
                break;
            }
        }
    }
}

bool gesturePollShake() {
    if (!bnoOK || (!filled && head < 10)) return false;
    // Use ring buffer data (no extra I2C read — prevents WiFi/I2C conflict).
    // Compute magnitude deviation from gravity over last few samples.
    int n = 8;
    float maxDev = 0;
    for (int i = 0; i < n; i++) {
        int idx = (head - 1 - i + N_SAMP * 2) % N_SAMP;
        float ax = ring[idx][0], ay = ring[idx][1], az = ring[idx][2];
        float mag = sqrtf(ax * ax + ay * ay + az * az);
        float dev = fabsf(mag - 9.8f);
        if (dev > maxDev) maxDev = dev;
    }
    return maxDev > 8.0f;
}

GestureResult gestureGetResult() {
    GestureResult gr;
    gr.label      = latestTopLabel;
    gr.confidence = latestTopConf;
    gr.valid      = latestValid;
    return gr;
}

int gestureGetScores(float* outScores, int maxLabels) {
    int count = gestureGetLabelCount();
    if (count > maxLabels) count = maxLabels;
    for (int i = 0; i < count; i++) outScores[i] = latestScores[i];
    return count;
}
