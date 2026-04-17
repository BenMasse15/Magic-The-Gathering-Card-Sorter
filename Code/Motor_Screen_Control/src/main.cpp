#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "wifi_setup.h"
#include <Arduino.h>
#include <stdio.h>
#include <SPI.h>
#include <EVE.h>

void getTypeLine();
bool connectWiFi();
void writeServoMicroseconds(int servoId, int us);
void GoForward(int servoId, int speed_us, int Time);
void Stop(int servoId, int Time);
void rotateServo(int servoId, int degrees, int holdTime);           // Standard servo only
void servoToDegrees(int servoId, int degrees);                      // Standard servo only
void rotateServoContinuous(int servoId, int speed, int durationMs); // FS90R continuous servo
void stopServoContinuous(int servoId);                              // FS90R continuous servo
void calibrateServo(int servoId, int maxRotationMs);                // FS90R calibration helper
void smartRotate(int servoId, int value, int durationMs);           // Mixed servo types
void moveAllToHome();                                               // Home position for all servos

String Type; // Global string to store the card type

// ===== SERVO TYPE CONFIGURATION =====
// Define servo types
#define SERVO_STANDARD 0   // SF006C - standard position servo (0-180°)
#define SERVO_CONTINUOUS 1 // FS90R - continuous rotation servo (speed control)

// ===== SCREEN PINS CONFIGURATION =====
#define EVE_SCK 13
#define EVE_MISO 14
#define EVE_MOSI 21
#define EVE_CS 47
#define EVE_PDN 45

// Configure your servos here
const int NUM_SERVOS = 5;
const int servoPins[NUM_SERVOS] = {1, 2, 3, 4, 5}; // GPIO pins
const int servoTypes[NUM_SERVOS] = {
    SERVO_STANDARD,   // Servo 0: Standard position servo
    SERVO_STANDARD,   // Servo 1: Standard position servo
    SERVO_CONTINUOUS, // Servo 2: Continuous rotation servo
    SERVO_CONTINUOUS, // Servo 3: Continuous rotation servo
    SERVO_CONTINUOUS  // Servo 4: Continuous rotation servo
};

// For dedicated channel mode: one channel per servo (max 8)
const int pwmChannels[NUM_SERVOS] = {0, 1, 2, 3, 4}; // PWM channels for each servo

// For shared channel mode: all servos share one channel (unlimited servos)
const int SHARED_PWM_CHANNEL = 0; // All servos use this channel (one at a time)
int currentActiveServo = -1;

const int pwmFreq = 50;                  // 50 Hz for standard servo timing
const int pwmResolution = 14;            // bits of resolution for duty calculation (max 14 for ESP32-S3)
const int period_us = 1000000 / pwmFreq; // microseconds per period (should be ~20000)

void setup()
{
  Serial.begin(115200);
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  delay(2000);
  pinMode(EVE_CS, OUTPUT);
  digitalWrite(EVE_CS, HIGH);
  pinMode(EVE_PDN, OUTPUT);
  digitalWrite(EVE_PDN, LOW);

#if defined(ESP32)
#if defined(EVE_USE_ESP_IDF)
  /* not using the Arduino SPI class in order to use DMA */
  EVE_init_spi();
#else
  /* using the Arduino SPI class to be compatible with other devices */
  SPI.begin(EVE_SCK, EVE_MISO, EVE_MOSI);
#endif
#elif defined(ARDUINO_NUCLEO_F446RE) || defined(WIZIOPICO) || defined(PICOPI)
  /* not using the Arduino SPI class in order to use DMA */
  EVE_init_spi();
#else
  SPI.begin(); /* sets up the SPI to run in Mode 0 and 1 MHz */
  /* switch to 8MHz, note, init must be done with <11MHz */
  SPI.beginTransaction(SPISettings(8UL * 1000000UL, MSBFIRST, SPI_MODE0));
#endif
  if (E_OK == EVE_init()) /* make sure the init finished correctly */
  {
    EVE_cmd_dl(CMD_DLSTART);                            /* instruct the co-processor to start a new display list */
    EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);          /* set the default clear color to white */
    EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG); /* clear the screen - this and the previous prevent artifacts between lists, attributes are the color, stencil and tag buffers */
    EVE_color_rgb(0x000000);                            /* set the color to black */
    EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2, 30, EVE_OPT_CENTER, "HELLO WORLD!");
    EVE_cmd_text(30, 25, 22, EVE_OPT_CENTER, "Creatures: 25");
    EVE_cmd_dl(DL_DISPLAY); /* mark the end of the display-list */
    EVE_cmd_dl(CMD_SWAP);   /* make this list active */
                            //        EVE_execute_cmd(); /* wait for EVE to be no longer busy */
  }

  ledcSetup(SHARED_PWM_CHANNEL, pwmFreq, pwmResolution);
  Serial.println("Servo mode: SHARED CHANNEL (one at a time)");

  Serial.println("Number of servos: " + String(NUM_SERVOS));
  Serial.println("Servo Configuration:");
  for (int i = 0; i < NUM_SERVOS; i++)
  {
    String typeStr = (servoTypes[i] == SERVO_STANDARD) ? "Standard (SF006C)" : "Continuous (FS90R)";
    Serial.println("  Servo " + String(i) + ": " + typeStr + " on pin " + String(servoPins[i]));
  }

  /*   if (!connectWiFi())
    {
      Serial.println("[FATAL] WiFi failed. Rebooting...");
      delay(2000);
      ESP.restart();
    } */
  moveAllToHome(); // Move all servos to their home position at startup
}
typedef enum
{
  STATE_NEUTRAL,
  STATE_CREATURE,
  STATE_INSTANT,
  STATE_SORCERY,
  STATE_ENCHANTMENT,
  STATE_ARTIFACT,
  STATE_PLANESWALKER,
  STATE_LAND,
  STATE_UNKNOWN
} State_t;

typedef enum
{
  EVENT_BUTTON_PRESSED,
  EVENT_NONE
} Event_t;

void loop()
{

  State_t currentState = STATE_NEUTRAL;
  Event_t event = EVENT_NONE;

  while (true)
  {
    // getTypeLine();
    // delay(3000);
    switch (currentState)
    {
    case STATE_NEUTRAL:
      rotateServo(0, 0, 1000); // Rotate servo 1 to 0° and hold for 1 second
      rotateServo(1, 85, 1000);
      // Prompt for card type at the start
      Serial.println("\n\n=== ENTER CARD TYPE ===");
      Serial.println("Valid types: Creature, Instant, Sorcery, Enchantment, Artifact, Planeswalker, Land");
      Serial.print("Enter card type: ");

      while (Serial.available() == 0)
      {
        delay(100);
      }
      Type = Serial.readStringUntil('\n');
      Type.trim(); // Remove any whitespace
      Serial.println("Card type set to: " + Type);
      delay(1000);
      if (Type == "Creature")
      {
        currentState = STATE_CREATURE;
        Serial.println("Transitioning to CREATURE state");
      }
      else if (Type == "Instant")
      {
        currentState = STATE_INSTANT;
        Serial.println("Transitioning to INSTANT state");
      }
      else if (Type == "Sorcery")
      {
        currentState = STATE_SORCERY;
        Serial.println("Transitioning to SORCERY state");
      }
      else if (Type == "Enchantment")
      {
        currentState = STATE_ENCHANTMENT;
        Serial.println("Transitioning to ENCHANTMENT state");
      }
      else if (Type == "Artifact")
      {
        currentState = STATE_ARTIFACT;
        Serial.println("Transitioning to ARTIFACT state");
      }
      else if (Type == "Planeswalker")
      {
        currentState = STATE_PLANESWALKER;
        Serial.println("Transitioning to PLANESWALKER state");
      }
      else if (Type == "Land")
      {
        currentState = STATE_LAND;
        Serial.println("Transitioning to LAND state");
      }
      else
      {
        currentState = STATE_UNKNOWN;
        Serial.println("Transitioning to UNKNOWN state");
      }

      break;
    case STATE_LAND:
      rotateServo(0, 135, 1000);
      rotateServo(1, 40, 1000);
      rotateServo(1, 85, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("LAND SORTED");
      break;

    case STATE_CREATURE:
      rotateServo(1, 120, 1000);
      rotateServo(1, 85, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("CREATURE SORTED");
      break;

    case STATE_ARTIFACT:
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("ARTIFACT SORTED");
      break;

    case STATE_ENCHANTMENT:
      rotateServo(0, 180, 1000);
      rotateServo(1, 40, 1000);
      rotateServo(1, 85, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("ENCHANTMENT SORTED");
      break;

    case STATE_INSTANT:
      rotateServo(0, 90, 1000);
      rotateServo(1, 40, 1000);
      rotateServo(1, 85, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("INSTANT SORTED");
      break;
    case STATE_SORCERY:
      rotateServo(0, 135, 1000);
      rotateServo(1, 40, 1000);
      rotateServo(1, 85, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("SORCERY SORTED");
      break;
    case STATE_PLANESWALKER:
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("PLANESWALKER SORTED");
      break;

    case STATE_UNKNOWN:
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("UNKNOWN SORTED");
      break;
    }
  }
  // rotateServo(0, 1000);   // Rotate to 0° and hold for 1 second
  // rotateServo(90, 1000);  // Rotate to 90° and hold for 1 second
  // rotateServo(180, 1000); // Rotate to 180° and hold for 1 second
}

void getTypeLine()
{
  WiFiClientSecure client;
  client.setInsecure(); // IMPORTANT for HTTPS on ESP32-S3

  HTTPClient http;
  http.begin(client, "https://ocr-server-ozql.onrender.com/last-card");

  int code = http.GET();
  if (code > 0)
  {
    String payload = http.getString();

    JsonDocument doc;
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

void writeServoMicroseconds(int servoId, int us)
{
  if (servoId < 0 || servoId >= NUM_SERVOS)
    return; // Validate servo ID
  // Shared channel mode: switch to this servo before writing
  if (currentActiveServo != servoId)
  {
    // Detach old servo and attach new one
    if (currentActiveServo >= 0)
      ledcDetachPin(servoPins[currentActiveServo]);
    ledcAttachPin(servoPins[servoId], SHARED_PWM_CHANNEL);
    currentActiveServo = servoId;
  }
  int channel = SHARED_PWM_CHANNEL;

  // duty = us / period_us * (2^resolution - 1)
  uint32_t maxDuty = (1UL << pwmResolution) - 1UL;
  // Use 64-bit math to avoid overflow
  uint32_t duty = (uint32_t)((uint64_t)us * maxDuty / (uint64_t)period_us);
  ledcWrite(channel, duty);
}

void GoForward(int servoId, int speed_us, int Time)
{
  writeServoMicroseconds(servoId, speed_us); // speed_us > 1500
  delay(Time);
}

void Stop(int servoId, int Time)
{
  writeServoMicroseconds(servoId, 1500); // stop
  delay(Time);
}

void servoToDegrees(int servoId, int degrees)
{
  if (servoId < 0 || servoId >= NUM_SERVOS)
    return;

  int minPulse = 500; // full range
  int maxPulse = 2500;

  int us = minPulse + (degrees * (maxPulse - minPulse)) / 180;

  writeServoMicroseconds(servoId, us);
}

// ===== FOR CONTINUOUS ROTATION SERVOS (like FS90R) =====
// Rotate FS90R servo at a given speed and direction
// servoId: which servo to control
// speed: -255 to 255 (-255 = full speed reverse, 0 = stop, 255 = full speed forward)
// durationMs: how long to rotate (in milliseconds)
void rotateServoContinuous(int servoId, int speed, int durationMs)
{
  if (servoId < 0 || servoId >= NUM_SERVOS)
    return;

  // Clamp speed to valid range
  if (speed < -255)
    speed = -255;
  if (speed > 255)
    speed = 255;

  // Convert speed to microseconds:
  // speed = -255: 1000us (full reverse)
  // speed = 0: 1500us (stop)
  // speed = 255: 2000us (full forward)
  int us = 1500 + (speed * 500) / 255;

  writeServoMicroseconds(servoId, us);
  delay(durationMs);
}

// Stop a continuous rotation servo
void stopServoContinuous(int servoId)
{
  writeServoMicroseconds(servoId, 1500); // 1500us = stop
}

void rotateServo(int servoId, int degrees, int holdTime)
{
  // Rotate to specified angle and hold for holdTime milliseconds
  servoToDegrees(servoId, degrees);
  delay(holdTime);
}

// Move all servos to their home (neutral) position
void moveAllToHome()
{
  for (int i = 0; i < NUM_SERVOS; i++)
  {
    if (servoTypes[i] == SERVO_STANDARD)
    {
      rotateServo(i, 90, 0); // Standard servos to 90°
    }
    else
    {
      stopServoContinuous(i); // Continuous servos to stop
    }
  }
}
