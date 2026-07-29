/*
 * mqtt_client.h — MQTT publish module.
 *
 * Connects to the broker over WiFi (STA mode) and exposes three publish
 * helpers that match the topics defined in config.h:
 *
 *   mqttPublishConfig()  — retained gesture/pokemon table (sent on connect)
 *   mqttPublishGesture() — real-time inferred gesture (before touch)
 *   mqttPublishResult()  — final reveal (gesture, pokemon, hidden flag)
 *
 * The web dashboard subscribes to these topics.
 */
#pragma once
#include <Arduino.h>

bool mqttBegin();

// Call every loop iteration to keep the connection alive.
void mqttLoop();

// Publish helpers.
void mqttPublishConfig();
void mqttPublishGesture(const char* gestureLabel);
void mqttPublishResult(const char* gestureLabel, const char* pokemonName,
                       bool hidden);
void mqttPublishScores(const char* const* labels, const float* scores,
                       int count);

// True when the broker connection is currently live.
bool mqttConnected();
