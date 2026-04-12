#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "wifi_setup.h"
#include <Arduino.h>

void getTypeLine();
bool connectWiFi();

void setup()
{
  Serial.begin(115200);
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  delay(500);
  Serial.println("\n=== ESP32-S3 MOTOR CONTROL ===");

  if (!connectWiFi())
  {
    Serial.println("[FATAL] WiFi failed. Rebooting...");
    delay(2000);
    ESP.restart();
  }
}

void loop()
{
  getTypeLine();
  delay(3000);
}

void getTypeLine()
{
  WiFiClientSecure client;
  client.setInsecure();  // IMPORTANT for HTTPS on ESP32-S3

  HTTPClient http;
  http.begin(client, "https://ocr-server-ozql.onrender.com/last-card");

  int code = http.GET();
  if (code > 0)
  {
    String payload = http.getString();

    DynamicJsonDocument doc(20000);
    DeserializationError err = deserializeJson(doc, payload);

    if (err)
    {
      Serial.println("JSON parse error");
      Serial.println(err.c_str());
      return;
    }

    const char *typeLine = doc["card"]["type_line"];
    const char *name = doc["card"]["name"];

    Serial.println(typeLine);
    Serial.println(name);
  }
  else
  {
    Serial.print("HTTP Error: ");
    Serial.println(code);
  }

  http.end();
}

bool connectWiFi()
{
  Serial0.println("[WiFi] Connecting...");

  WiFi.begin(ssid, password);
  uint32_t start = millis();

  while (WiFi.status() != WL_CONNECTED)
  {
    delay(300);
    Serial0.print(".");
    if (millis() - start > 15000)
    {
      Serial0.println("\n[WiFi] Connection timeout");
      return false;
    }
  }

  Serial0.print("\n[WiFi] Connected! IP: ");
  Serial0.println(WiFi.localIP());
  return true;
}
