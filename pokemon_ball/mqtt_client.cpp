/*
 * mqtt_client.cpp — MQTT publish module implementation.
 *
 * Reuses the PubSubClient + WiFi STA pattern from esp32_2/SensorMqtt.
 * The reconnect loop is non-blocking-friendly: it attempts one connect,
 * and if it fails returns immediately so the main loop can keep running
 * (display, touch, etc.) instead of stalling.
 */
#include "mqtt_client.h"
#include "config.h"
#include <WiFi.h>
#include <PubSubClient.h>

static WiFiClient    espClient;
static PubSubClient  client(espClient);
static unsigned long lastReconnect = 0;

// -----------------------------------------------------------------------
// Internal
// -----------------------------------------------------------------------
static void connectWifi() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setTxPower(WIFI_POWER_15dBm);   // BNO055 brownout workaround
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print(F("[MQTT] WiFi"));
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(F("."));
    }
    Serial.printf(" %s\n", WiFi.localIP().toString().c_str());
}

static bool mqttConnect() {
    Serial.print(F("[MQTT] connecting... "));
    if (client.connect(MQTT_CLIENT_ID)) {
        Serial.println(F("connected"));
        mqttPublishConfig();   // send retained config on every (re)connect
        return true;
    }
    Serial.printf("failed rc=%d\n", client.state());
    return false;
}

// -----------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------

bool mqttBegin() {
    connectWifi();

    client.setServer(MQTT_HOST, MQTT_PORT);
    client.setBufferSize(1024);  // ensure room for the config JSON
    client.setKeepAlive(30);

    mqttConnect();
    return true;
}

void mqttLoop() {
    if (!client.connected()) {
        // Throttle reconnection attempts to every 5 s.
        if (millis() - lastReconnect >= 5000) {
            lastReconnect = millis();
            mqttConnect();
        }
    }
    client.loop();
}

bool mqttConnected() {
    return client.connected();
}

// -----------------------------------------------------------------------
// JSON builders
// -----------------------------------------------------------------------

void mqttPublishConfig() {
    // Build: {"hidden":"Sanjini","hiddenProb":2,"mappings":[
    //           {"gesture":"GESTURE_A","pokemon":"Pokemon_A"}, ... ]}
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

    // retained = true so a web page that connects later immediately gets it
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

void mqttPublishResult(const char* gestureLabel, const char* pokemonName,
                       bool hidden) {
    String json = "{\"gesture\":\"";
    json += gestureLabel ? gestureLabel : "";
    json += "\",\"pokemon\":\"";
    json += pokemonName ? pokemonName : "";
    json += "\",\"hidden\":";
    json += hidden ? "true" : "false";
    json += "}";

    // retained = true so the "latest result" survives a page reload
    client.publish(TOPIC_RESULT, json.c_str(), true);
    Serial.printf("[MQTT] result: %s → %s (hidden=%s)\n",
                  gestureLabel ? gestureLabel : "?",
                  pokemonName ? pokemonName : "?",
                  hidden ? "yes" : "no");
}
