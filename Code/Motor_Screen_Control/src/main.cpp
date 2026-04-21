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
String normalizeType(String raw);
bool connectWiFi();
void writeServoMicroseconds(int servoId, int us);
void GoForward(int servoId, int speed_us, int Time);
void Stop(int servoId, int Time);
void rotateServo(int servoId, int degrees, int holdTime);
void rotateServoSlow(int servoId, int targetDeg, int speedDelayMs, int holdTimeMs); // Standard servo only
void servoToDegrees(int servoId, int degrees);                                      // Standard servo only
void rotateServoContinuous(int servoId, int speed, int durationMs);                 // FS90R continuous servo
void stopServoContinuous(int servoId);                                              // FS90R continuous servo
void calibrateServo(int servoId, int maxRotationMs);                                // FS90R calibration helper
void smartRotate(int servoId, int value, int durationMs);                           // Mixed servo types
void moveAllToHome();                                                               // Home position for all servos
void updateEveScreen();
void screenInit();
bool resetButtonPressed();
void resetCounters();
String Type; // Global string to store the card type
String normalized;
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

int ArtifactCounter = 0;
int EnchantmentCounter = 0;
int OthersCounter = 0;
int LandCounter = 0;
int SorceryCounter = 0;
int InstantCounter = 0;
int CreatureCounter = 0;
int UnknownCounter = 0;
int Scan_Attempts = 0;

volatile bool resetRequested = false;
void IRAM_ATTR handleResetInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last > 200)
  { // debounce
    resetRequested = true;
  }
  last = now;
}

volatile bool ManaShuffle = false;
void IRAM_ATTR handleManaShuffleInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last > 200)
  { // debounce
    ManaShuffle = true;
  }
  last = now;
}

volatile bool Sorting = false;
void IRAM_ATTR handleSortingInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();
  if (now - last > 200)
  { // debounce
    Sorting = true;
  }
  last = now;
}

void setup()
{
  Serial.begin(115200);
  Serial0.begin(115200, SERIAL_8N1, 44, 43);
  pinMode(15, INPUT_PULLUP);
  pinMode(16, INPUT_PULLUP);
  pinMode(17, INPUT_PULLUP);
  pinMode(18, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(15), handleResetInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(16), handleManaShuffleInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(17), handleSortingInterrupt, FALLING);
  delay(2000);
  screenInit();

  ledcSetup(SHARED_PWM_CHANNEL, pwmFreq, pwmResolution);
  Serial.println("Servo mode: SHARED CHANNEL (one at a time)");

  Serial.println("Number of servos: " + String(NUM_SERVOS));
  Serial.println("Servo Configuration:");
  for (int i = 0; i < NUM_SERVOS; i++)
  {
    String typeStr = (servoTypes[i] == SERVO_STANDARD) ? "Standard (SF006C)" : "Continuous (FS90R)";
    Serial.println("  Servo " + String(i) + ": " + typeStr + " on pin " + String(servoPins[i]));
  }
  updateEveScreen();
  if (!connectWiFi())
  {
    Serial.println("[FATAL] WiFi failed. Rebooting...");
    delay(2000);
    ESP.restart();
  }
  rotateServoSlow(0, 0, 5, 1000);
  rotateServoSlow(1, 90, 5, 1000);
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

  while (true)
  {
    switch (currentState)
    {
    case STATE_NEUTRAL:
      rotateServoSlow(0, 0, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);

      if (resetRequested)
      {
        resetRequested = false;
        resetCounters();
        updateEveScreen();
        break;
      }

      if (ManaShuffle)
      {
        ManaShuffle = false;
        Serial.println("\n=== Mana Shuffle Initiated ===");
        break;
      }

      if (Sorting)
      {
        Serial.println("\n=== Sorting Initiated ===");
        rotateServoContinuous(2, LandCounter * 600, 200); // Full forward for 2 seconds
        writeServoMicroseconds(2, 1500); // Stop servo 2
        rotateServoContinuous(3, CreatureCounter * 600, 200); // Full forward for 2 seconds
        writeServoMicroseconds(3, 1500); // Stop servo 3
        rotateServoContinuous(7, SorceryCounter * 600, 200); // Full forward for 2 seconds
        writeServoMicroseconds(7, 1500); // Stop servo 7
        Sorting = false;
        break;
      }

      Serial.println("\n=== WAITING FOR CARD ===");
      getTypeLine(); // fetch from /last-card, sets global `normalized`

      if (normalized == "" || normalized == "Unknown")
      {
        Serial.println("No valid card yet, retrying...");
        Scan_Attempts++;
        if (Scan_Attempts == 2)
        {
          rotateServoSlow(1, 91, 50, 0);
        }
        if (Scan_Attempts == 3)
        {
          rotateServoSlow(1, 89, 50, 0);
        }
        delay(750);
        break;
      }

      Serial.println("Got type: " + normalized);
      Scan_Attempts = 0; // reset attempts on valid read
      if (normalized == "Creature")
        currentState = STATE_CREATURE;
      else if (normalized == "Instant")
        currentState = STATE_INSTANT;
      else if (normalized == "Sorcery")
        currentState = STATE_SORCERY;
      else if (normalized == "Enchantment")
        currentState = STATE_ENCHANTMENT;
      else if (normalized == "Artifact")
        currentState = STATE_ARTIFACT;
      else if (normalized == "Planeswalker" || normalized == "Battle" || normalized == "Kindred")
      currentState = STATE_PLANESWALKER;
      else if (normalized == "Land")
        currentState = STATE_LAND;
      else
        currentState = STATE_UNKNOWN;

      break;
    case STATE_LAND:
      LandCounter++;
      updateEveScreen();
      rotateServoSlow(0, 135, 50, 1000);
      rotateServoSlow(1, 40, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("LAND SORTED");
      break;

    case STATE_CREATURE:
      CreatureCounter++;
      updateEveScreen();
      rotateServoSlow(1, 130, 5, 1000);
      rotateServoSlow(1, 90, 10, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("CREATURE SORTED");
      break;

    case STATE_ARTIFACT:
      ArtifactCounter++;
      rotateServoSlow(0, 45, 50, 1000);
      rotateServoSlow(1, 130, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      updateEveScreen();
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("ARTIFACT SORTED");
      break;

    case STATE_ENCHANTMENT:
      EnchantmentCounter++;
      updateEveScreen();
      rotateServoSlow(0, 90, 50, 1000);
      rotateServoSlow(1, 130, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("ENCHANTMENT SORTED");
      break;

    case STATE_INSTANT:
      InstantCounter++;
      updateEveScreen();
      rotateServoSlow(0, 135, 50, 1000);
      rotateServoSlow(1, 130, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("INSTANT SORTED");
      break;
    case STATE_SORCERY:
      SorceryCounter++;
      updateEveScreen();
      rotateServoSlow(1, 40, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("SORCERY SORTED");
      break;
    case STATE_PLANESWALKER:
      OthersCounter++;
      updateEveScreen();
      rotateServoSlow(0, 45, 50, 1000);
      rotateServoSlow(1, 40, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("PLANESWALKER SORTED");
      break;

    case STATE_UNKNOWN:
      UnknownCounter++;
      updateEveScreen();
      rotateServoSlow(0, 90, 50, 1000);
      rotateServoSlow(1, 40, 50, 1000);
      rotateServoSlow(1, 90, 50, 1000);
      currentState = STATE_NEUTRAL; // Transition to next state
      Serial.println("UNKNOWN SORTED");
      break;
    }
  }
}

void getTypeLine()
{
  normalized = ""; // clear before fetch

  WiFiClientSecure client;
  client.setInsecure();

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
      http.end();
      return;
    }

    const char *typeLine = doc["card"]["type_line"];
    const char *name = doc["card"]["name"];

    if (typeLine)
    {
      Serial.print("Type line: ");
      Serial.println(typeLine);
      Serial.print("Name: ");
      Serial.println(name ? name : "unknown");
      normalized = normalizeType(String(typeLine));
      HTTPClient http2;
      WiFiClientSecure client2;
      client2.setInsecure();
      http2.begin(client2, "https://ocr-server-ozql.onrender.com/clear-card");
      http2.POST("");
      http2.end();
    }
    else
    {
      Serial.println("No type_line in response");
    }
  }
  else
  {
    Serial.print("HTTP Error: ");
    Serial.println(code);
  }

  http.end();
}

String normalizeType(String raw)
{
  raw.toLowerCase();

  if (raw.indexOf("creature") != -1)    return "Creature";
  if (raw.indexOf("planeswalker") != -1) return "Planeswalker";
  if (raw.indexOf("battle") != -1)      return "Battle";
  if (raw.indexOf("kindred") != -1)     return "Kindred";
  if (raw.indexOf("instant") != -1)     return "Instant";
  if (raw.indexOf("sorcery") != -1)     return "Sorcery";
  if (raw.indexOf("enchantment") != -1) return "Enchantment";
  if (raw.indexOf("artifact") != -1)    return "Artifact";
  if (raw.indexOf("land") != -1)        return "Land";

  return "Unknown";
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

  if (servoTypes[servoId] == SERVO_CONTINUOUS && us == 1500)
  {
    // Re-send stop pulse to prevent drift
    ledcWrite(channel, duty);
  }
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

void rotateServoSlow(int servoId, int targetDeg, int speedDelayMs, int holdTimeMs)
{
  // Track last known position for each servo
  static int lastPos[NUM_SERVOS] = {90, 90, 90, 90, 90};

  int current = lastPos[servoId];

  // Determine direction
  int step = (targetDeg > current) ? 1 : -1;

  // Smooth stepping loop
  while (current != targetDeg)
  {
    current += step;
    servoToDegrees(servoId, current);
    delay(speedDelayMs); // <-- controls speed
  }

  // Save final position
  lastPos[servoId] = current;

  // Hold at final angle
  if (holdTimeMs > 0)
    delay(holdTimeMs);
}

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

void updateEveScreen()
{
  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);

  EVE_color_rgb(0x000000);

  // Title
  EVE_cmd_text(EVE_HSIZE / 2, 20, 30, EVE_OPT_CENTER, "MTG SORT COUNTS");

  // Print each counter
  char buffer[64];

  sprintf(buffer, "Creatures: %d", CreatureCounter);
  EVE_cmd_text(30, 60, 22, 0, buffer);

  sprintf(buffer, "Instants: %d", InstantCounter);
  EVE_cmd_text(30, 90, 22, 0, buffer);

  sprintf(buffer, "Sorceries: %d", SorceryCounter);
  EVE_cmd_text(30, 120, 22, 0, buffer);

  sprintf(buffer, "Enchantments: %d", EnchantmentCounter);
  EVE_cmd_text(30, 150, 22, 0, buffer);

  sprintf(buffer, "Artifacts: %d", ArtifactCounter);
  EVE_cmd_text(30, 180, 22, 0, buffer);

  sprintf(buffer, "Planeswalkers: %d", OthersCounter);
  EVE_cmd_text(30, 210, 22, 0, buffer);

  sprintf(buffer, "Lands: %d", LandCounter);
  EVE_cmd_text(30, 240, 22, 0, buffer);

  sprintf(buffer, "Unknown: %d", UnknownCounter);
  EVE_cmd_text(30, 270, 22, 0, buffer);

  EVE_cmd_dl(DL_DISPLAY);
  EVE_cmd_dl(CMD_SWAP);
}

void screenInit()
{
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
}

bool resetButtonPressed()
{
  static uint32_t lastTime = 0;
  static bool lastState = HIGH;

  bool reading = digitalRead(15);

  if (reading != lastState)
  {
    lastTime = millis();
    lastState = reading;
  }

  // Debounce window
  if ((millis() - lastTime) > 30)
  {
    if (reading == LOW) // button pressed
      return true;
  }

  return false;
}

void resetCounters()
{
  CreatureCounter = 0;
  InstantCounter = 0;
  SorceryCounter = 0;
  EnchantmentCounter = 0;
  ArtifactCounter = 0;
  OthersCounter = 0;
  LandCounter = 0;
  UnknownCounter = 0;

  Serial.println("=== COUNTERS RESET ===");
  updateEveScreen();
}
