/*
 * gesture.cpp — BNO055 data collection + Edge Impulse inference.
 *
 * Reuses the raw-I2C read pattern from edgeImpulse/gesture_infer because
 * the Adafruit BNO055 library can return zeros while WiFi is active.
 *
 * The EI integration mirrors gesture_infer.ino exactly:
 *   - 100 samples at 50 Hz (6 axes each → 600-element feature buffer)
 *   - ei::signal_t + run_classifier()
 *   - pick the highest-confidence label
 *
 * A compile-time stub (when USE_EDGE_IMPULSE is not defined) lets the full
 * program run without the trained model.  The stub collects the same 2 s
 * of real BNO055 data, then returns a pseudo-gesture derived from the
 * dominant movement axis so the pipeline is end-to-end testable today.
 */
#include "gesture.h"
#include "config.h"
#include <Wire.h>
#include <Adafruit_BNO055.h>
#include <utility/imumaths.h>

#ifdef USE_EDGE_IMPULSE
#include "gesture_inferencing.h"
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
// Raw I2C read — bypasses the Adafruit library (WiFi-safe).
//   reg 0x08  accel (scale 0.01 → m/s²)
//   reg 0x14  gyro  (scale 1/900 → rad/s)
//   reg 0x28  linear accel (scale 0.01 → m/s², gravity removed)
// -----------------------------------------------------------------------
static void readBNORaw(uint8_t reg, float& x, float& y, float& z, float scale) {
    Wire.beginTransmission(BNO055_ADDR);
    Wire.write(reg);
    Wire.endTransmission();
    Wire.requestFrom((int)BNO055_ADDR, (int)6);
    if (Wire.available() >= 6) {
        int16_t rx = Wire.read() | (Wire.read() << 8);
        int16_t ry = Wire.read() | (Wire.read() << 8);
        int16_t rz = Wire.read() | (Wire.read() << 8);
        x = rx * scale; y = ry * scale; z = rz * scale;
    } else {
        x = y = z = 0;
    }
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool gestureBegin() {
    Wire.begin(I2C_SDA, I2C_SCL);
    Wire.setClock(400000);

    if (bno.begin()) {
        bno.setMode(OPERATION_MODE_NDOF);
        bnoOK = true;
        Serial.println(F("[GESTURE] BNO055 OK"));
    } else {
        Serial.println(F("[GESTURE] BNO055 FAIL"));
    }
    return bnoOK;
}

// Re-apply NDOF after WiFi brought the bus up (brownout workaround
// from gesture_infer.ino).
void gesturePostWifiInit() {
    if (bnoOK) {
        bno.setMode(OPERATION_MODE_NDOF);
        delay(50);
    }
}

bool gesturePollShake() {
    if (!bnoOK) return false;
    float x, y, z;
    readBNORaw(0x28, x, y, z, 0.01);  // linear acceleration (gravity-free)
    float mag = sqrtf(x * x + y * y + z * z);
    return mag > SHAKE_THRESHOLD;
}

GestureResult gestureInfer() {
    GestureResult gr = { nullptr, 0.0f, false };
    if (!bnoOK) return gr;

    // ---- Collect 2 s of accel + gyro at 50 Hz ---------------------------
    // Accumulators for the stub fallback.
    float sumAx = 0, sumAy = 0, sumAz = 0;
    unsigned long nextSample = millis();

    for (int i = 0; i < INFERENCE_SAMPLES; i++) {
        nextSample += INFERENCE_SAMPLE_MS;
        while ((long)(millis() - nextSample) < 0) delay(1);

        float ax, ay, az, gx, gy, gz;
        readBNORaw(0x08, ax, ay, az, 0.01);    // accel m/s² (with gravity)
        readBNORaw(0x14, gx, gy, gz, 1.0f / 900.0f);  // gyro rad/s

#ifdef USE_EDGE_IMPULSE
        int idx = i * EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
        features[idx]     = ax;
        features[idx + 1] = ay;
        features[idx + 2] = az;
        features[idx + 3] = gx;
        features[idx + 4] = gy;
        features[idx + 5] = gz;
#endif
        sumAx += ax; sumAy += ay; sumAz += az;
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
    // Mean acceleration (gravity ~9.8 on one axis).  Subtract the static
    // component so only the dynamic shake remains, then pick the axis with
    // the largest absolute mean deviation.
    sumAx /= INFERENCE_SAMPLES;
    sumAy /= INFERENCE_SAMPLES;
    sumAz /= INFERENCE_SAMPLES;

    // Remove approximate gravity (Z usually ~9.8 at rest).
    float dx = sumAx;
    float dy = sumAy;
    float dz = sumAz - 9.8f;

    int pick = 0;
    float a = fabsf(dx), b = fabsf(dy), c = fabsf(dz);
    if (a >= b && a >= c)      pick = 0;
    else if (b >= a && b >= c) pick = 1;
    else                       pick = 2;

    // Map the three axis groups to table entries, cycling for variety so
    // all code paths (display, MQTT, web) get exercised during testing.
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
