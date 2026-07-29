/*
 * mqtt_client.h — MQTT publish module.
 */
#pragma once
#include <Arduino.h>

bool mqttBegin();
void mqttLoop();
void mqttPublishConfig();
void mqttPublishGesture(const char* gestureLabel);
void mqttPublishPredict(const char* const* labels, const float* scores, int count);
void mqttPublishCapture(const char* gestureLabel, const char* pokemonName, bool hidden);
bool mqttConnected();
