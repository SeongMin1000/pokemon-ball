/*
 * gesture.cpp — BNO055 data collection + Edge Impulse inference.
 *
 * Based on bee2/edgeImpulse/gesture_infer — Adafruit getVector (proven
 * WiFi-safe in STA mode), 100 Hz sampling, gyro in dps.
 *
 * The EI integration mirrors bee2's gesture_infer.ino:
 *   - EI_CLASSIFIER_RAW_SAMPLE_COUNT samples at EI_CLASSIFIER_FREQUENCY Hz
 *     (6 axes each → EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE feature buffer)
 *   - ei::signal_t + run_classifier()
 *   - pick the highest-confidence label
 *
 * A compile-time stub (when USE_EDGE_IMPULSE is not defined) lets the full
 * program run without the trained model.  The stub collects the same window
 * of real BNO055 data, then returns a pseudo-gesture so the pipeline is
 * end-to-end testable today.
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

#ifdef USE_EDGE_IMPULSE
static float features[EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE];

// EI signal callback — copies a slice of the feature buffer.
static int get_signal_data(size_t offset, size_t length, float* out_ptr) {
    memcpy(out_ptr, features + offset, length * sizeof(float));
    return 0;
}
#endif

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool gestureBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    if (bno.begin()) {
        bno.setExtCrystalUse(true);
        bnoOK = true;
        Serial.println(F("[GESTURE] BNO055 OK"));
    } else {
        Serial.println(F("[GESTURE] BNO055 FAIL"));
    }
    return bnoOK;
}

void gesturePostWifiInit() {
    // bee2 proved getVector works after WiFi STA — no mode re-assert needed,
    // but we keep this as a no-op hook for brownout recovery if ever required.
}

bool gesturePollShake() {
    if (!bnoOK) return false;
    // VECTOR_LINEARACCEL = gravity-free acceleration (m/s²)
    imu::Vector<3> la = bno.getVector(Adafruit_BNO055::VECTOR_LINEARACCEL);
    float mag = sqrtf(la.x() * la.x() + la.y() * la.y() + la.z() * la.z());
    return mag > SHAKE_THRESHOLD;
}

GestureResult gestureInfer() {
    GestureResult gr = { nullptr, 0.0f, false };
    if (!bnoOK) return gr;

    // ---- Determine sample count & interval ------------------------------
    // When the EI model is present, use its constants.  Otherwise fall back
    // to the config.h defaults (100 Hz × 2 s = 200 samples).
#ifdef USE_EDGE_IMPULSE
    const int    nSamp   = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    const int    nAxes   = EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;   // 6
    const uint32_t intervalUs = (1000000UL / EI_CLASSIFIER_FREQUENCY);
#else
    const int    nSamp   = INFERENCE_SAMPLES;
    const int    nAxes   = 6;
    const uint32_t intervalUs = (1000000UL / (1000 / INFERENCE_SAMPLE_MS));
#endif

    // ---- Collect window of accel + gyro at precise intervals ------------
    // Uses micros() busy-wait (same as bee2/gesture_collect) for accurate
    // 100 Hz timing.  Accel = m/s² (includes gravity), gyro = dps.
    float sumAx = 0, sumAy = 0, sumAz = 0;   // stub accumulators

    uint32_t next = micros();
    for (int i = 0; i < nSamp; i++) {
        while ((int32_t)(micros() - next) < 0) { }
        imu::Vector<3> a = bno.getVector(Adafruit_BNO055::VECTOR_ACCELEROMETER);
        imu::Vector<3> g = bno.getVector(Adafruit_BNO055::VECTOR_GYROSCOPE);

        float ax = a.x(), ay = a.y(), az = a.z();
        float gx = g.x(), gy = g.y(), gz = g.z();

#ifdef USE_EDGE_IMPULSE
        int idx = i * nAxes;
        features[idx]     = ax;
        features[idx + 1] = ay;
        features[idx + 2] = az;
        features[idx + 3] = gx;
        features[idx + 4] = gy;
        features[idx + 5] = gz;
#endif
        sumAx += ax; sumAy += ay; sumAz += az;
        next += intervalUs;
    }

#ifdef USE_EDGE_IMPULSE
    // ---- Run the real Edge Impulse classifier ---------------------------
    ei::signal_t signal;
    signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    signal.get_data     = &get_signal_data;

    ei_impulse_result_t result;
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) {
        Serial.printf("[GESTURE] classifier error %d\n", err);
        return gr;
    }

    // Pick the best label.
    float bestVal = -1.0f;
    const char* bestLabel = nullptr;
    for (size_t i = 0; i < EI_CLASSIFIER_LABEL_COUNT; i++) {
        if (result.classification[i].value > bestVal) {
            bestVal   = result.classification[i].value;
            bestLabel = result.classification[i].label;
        }
    }
    gr.label      = bestLabel;
    gr.confidence = bestVal;
    gr.valid      = (bestVal >= MIN_CONFIDENCE);

#else
    // ---- Stub: derive a pseudo-gesture from dominant motion axis -------
    sumAx /= nSamp; sumAy /= nSamp; sumAz /= nSamp;

    // Remove approximate gravity (Z usually ~9.8 at rest).
    float dx = sumAx, dy = sumAy, dz = sumAz - 9.8f;

    int pick = 0;
    float a = fabsf(dx), b = fabsf(dy), c = fabsf(dz);
    if (a >= b && a >= c)      pick = 0;
    else if (b >= a && b >= c) pick = 1;
    else                       pick = 2;

    // Cycle through table entries for testing variety.
    pick = (pick + (millis() / 1000)) % GESTURE_COUNT;
    if (pick < 0) pick = 0;

    gr.label      = POKEMON_TABLE[pick].gestureLabel;
    gr.confidence = 0.85f;
    gr.valid      = true;
#endif

    // Discard "idle" labels.
    if (gr.valid && gr.label) {
        for (int j = 0; j < IDLE_LABEL_COUNT; j++) {
            if (strcmp(gr.label, IDLE_LABELS[j]) == 0) {
                gr.valid = false;
                break;
            }
        }
    }

    if (gr.valid) {
        Serial.printf("[GESTURE] %s (%.1f%%)\n", gr.label, gr.confidence * 100);
    } else {
        Serial.println(F("[GESTURE] idle / discarded"));
    }
    return gr;
}
