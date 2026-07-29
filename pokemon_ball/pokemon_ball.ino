/*
 * pokemon_ball.ino — Pokemon Ball: gesture-driven monster catcher.
 *
 * Hardware:  Seeed XIAO ESP32S3
 *            + Seeed Round Display (GC9A01 240×240)
 *            + GY-BNO055 IMU (I2C 0x29)
 *            + capacitive touch pad on GPIO1 (T1)
 *
 * Flow:
 *   1. Shake the board  →  BNO055 data → Edge Impulse inference
 *   2. Gesture stored temporarily (pokeball shows "TOUCH!" hint)
 *   3. Touch the pad    →  gesture confirmed → pokemon revealed
 *   4. 2% chance: hidden character "Sanjini" overrides the pokemon
 *   5. Result published via MQTT; web dashboard updates in real time
 *
 * Modules (see headers for public APIs):
 *   display     — round LCD (pokeball + pokemon images)
 *   gesture     — BNO055 + Edge Impulse (with stub fallback)
 *   touch       — debounced capacitive touch
 *   mqtt_client — WiFi STA + PubSubClient
 *   game        — state machine tying everything together
 *
 * Build:  arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 pokemon_ball
 * Upload: arduino-cli upload -p COMx --fqbn esp32:esp32:XIAO_ESP32S3 pokemon_ball
 */
#include <Arduino.h>
#include "config.h"
#include "display.h"
#include "gesture.h"
#include "touch.h"
#include "mqtt_client.h"
#include "game.h"

void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println(F("\n=== Pokemon Ball ==="));

    // --- LED (status indicator) ---
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, HIGH);   // off (inverted)

    // --- Display (shows pokeball immediately) ---
    displayBegin();

    // --- IMU / gesture ---
    gestureBegin();

    // --- Touch ---
    touchBegin();

    // --- WiFi + MQTT (STA mode, no AP) ---
    // WiFi.setTxPower is called inside mqttBegin() — re-assert BNO055 mode
    // afterwards to counter the known brownout issue.
    mqttBegin();
    gesturePostWifiInit();

    // --- Start the game state machine ---
    gameInit();

    Serial.println(F("[BOOT] ready. Shake to start!"));
    digitalWrite(LED_PIN, LOW);    // LED on = ready
}

void loop() {
    mqttLoop();   // keep MQTT alive (non-blocking reconnect)
    gameLoop();   // run the state machine
}
