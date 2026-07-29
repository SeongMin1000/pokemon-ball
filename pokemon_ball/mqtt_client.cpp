/*
 * mqtt_client.cpp — MQTT publish module implementation.
 */
#include "mqtt_client.h"
#include "config.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient    espClient;
static PubSubClient  client(espClient);
static unsigned long lastReconnect = 0;

// -----------------------------------------------------------------------
static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_15dBm);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("[MQTT] WiFi"));
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
        delay(500);
        Serial.print(F("."));
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println(F(" TIMEOUT — continuing without MQTT"));
    }
}

static bool mqttConnect() {
    Serial.print(F("[MQTT] connecting... "));
    if (client.connect(MQTT_CLIENT_ID)) {
        Serial.println(F("connected"));
        mqttPublishConfig();
        return true;
    }
    Serial.printf("failed rc=%d\n", client.state());
    return false;
}

// -----------------------------------------------------------------------
bool mqttBegin() {
    connectWifi();
    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setBufferSize(1024);
    client.setKeepAlive(30);
    mqttConnect();
    return true;
}

void mqttLoop() {
    if (!client.connected()) {
        if (millis() - lastReconnect >= 5000) {
            lastReconnect = millis();
            mqttConnect();
        }
    }
    client.loop();
}

bool mqttConnected() { return client.connected(); }

// -----------------------------------------------------------------------
void mqttPublishConfig() {
    String json = "{\"hidden\":\"";
    json += HIDDEN_NAME;
    json += "\",\"hiddenProb\":";
    json += HIDDEN_PROBABILITY;
    json += ",\"mappings\":[";
    for (int i = 0; i < GESTURE_COUNT; i++) {
        if (i > 0) json += ",";
        json += "{\"gesture\":\"";
        json += POKEMON_TABLE[i].gestureLabel;
        json += "\",\"pokemon\":\"";
        json += POKEMON_TABLE[i].pokemonName;
        json += "\"}";
    }
    json += "]}";
    client.publish(TOPIC_CONFIG, json.c_str(), true);
    Serial.printf("[MQTT] config (%d bytes)\n", (int)json.length());
}

void mqttPublishGesture(const char* gestureLabel) {
    String json = "{\"gesture\":\"";
    json += gestureLabel ? gestureLabel : "";
    json += "\"}";
    client.publish(TOPIC_GESTURE, json.c_str());
    Serial.printf("[MQTT] gesture: %s\n", gestureLabel ? gestureLabel : "null");
}

void mqttPublishPredict(const char* const* labels, const float* scores, int count) {
    // {"type":"predict","probabilities":{"LEFT":72,"RIGHT":15,...},"topGesture":"LEFT"}
    float bestVal = -1;
    const char* bestLabel = "";
    String json = "{\"type\":\"predict\",\"probabilities\":{";
    bool first = true;
    for (int i = 0; i < count; i++) {
        if (!labels[i]) continue;
        // Skip idle labels
        bool isIdle = false;
        for (int j = 0; j < IDLE_LABEL_COUNT; j++) {
            if (strcmp(labels[i], IDLE_LABELS[j]) == 0) { isIdle = true; break; }
        }
        if (isIdle) continue;

        int pct = (int)(scores[i] * 100 + 0.5f);
        if (!first) json += ",";
        first = false;
        // Uppercase label
        String ul = String(labels[i]);
        ul.toUpperCase();
        json += "\"" + ul + "\":" + String(pct);
        if (scores[i] > bestVal) { bestVal = scores[i]; bestLabel = labels[i]; }
    }
    json += "},\"topGesture\":\"";
    String tul = String(bestLabel);
    tul.toUpperCase();
    json += tul + "\"}";
    client.publish(TOPIC_PREDICT, json.c_str());
}

void mqttPublishCapture(const char* gestureLabel, const char* pokemonName, bool hidden) {
    String json = "{\"type\":\"capture\",\"gesture\":\"";
    json += gestureLabel ? gestureLabel : "";
    json += "\",\"pokemon\":\"";
    json += pokemonName ? pokemonName : "";
    json += "\",\"hidden\":";
    json += hidden ? "true" : "false";
    json += "}";
    client.publish(TOPIC_CAPTURE, json.c_str(), true);
    Serial.printf("[MQTT] capture: %s → %s (hidden=%s)\n",
                  gestureLabel ? gestureLabel : "?",
                  pokemonName ? pokemonName : "?",
                  hidden ? "yes" : "no");
}
