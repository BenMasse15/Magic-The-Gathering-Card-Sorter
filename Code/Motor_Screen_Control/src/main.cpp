#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "wifi_setup.h"
#include <Arduino.h>
#include <stdio.h>
#include <SPI.h>
#include <EVE.h>

// ===== FORWARD DECLARATIONS =====
void getTypeLine();
String normalizeType(String raw);
bool connectWiFi();
void updateEveScreen();
void screenInit();
void resetCounters();
void drawMenu(int selectedIndex);
void drawManaShufflePrompt(int selected);
bool runStartMode();
bool runManaShuffleMode();
bool runTestingMode();
bool runCalibrateMode();
int waitOKRelease();
void processCardType(String type);
int showMenu();
bool checkMenuHold();
void writeServoMicroseconds(int servoId, int us);
void GoForward(int servoId, int speed_us, int Time);
void Stop(int servoId, int Time);
void servoToDegrees(int servoId, int degrees);
void rotateServo(int servoId, int degrees, int holdTime);
void rotateServoSlow(int servoId, int targetDeg, int speedDelayMs, int holdTimeMs);
void rotateServoContinuous(int servoId, int speed, int durationMs);
void stopServoContinuous(int servoId);
void moveAllToHome();
void runSortingServo(int servoIdx, int counter);
void drawManaShufflePrompt(int selection);
// ===== SCREEN PINS =====
#define EVE_SCK 13
#define EVE_MISO 14
#define EVE_MOSI 21
#define EVE_CS 47
#define EVE_PDN 45

// ===== BUTTON PINS =====
#define BTN_OK 35
#define BTN_UP 15
#define BTN_DOWN 17
#define BTN_RIGHT 16
#define BTN_LEFT 18

#define MENU_HOLD_MS 2000  // hold OK for 2s to return to menu
#define ADJUST_BACK_MS 800 // hold OK < 2s but > 0.8s = back to grid
// ===== GLOBALS =====
String Type;
String normalized;

int ArtifactCounter = 0;
int EnchantmentCounter = 0;
int OthersCounter = 0;
int LandCounter = 0;
int SorceryCounter = 0;
int InstantCounter = 0;
int CreatureCounter = 0;
int UnknownCounter = 0;
int Scan_Attempts = 0;
int One_Card_Delay = 1600;

String currentStatus = "Idle";
String lastCardType = "---";

volatile bool okPressed = false;
volatile uint32_t okPressStart = 0;
void IRAM_ATTR handleOKInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();

  if (now - last > 50)
  {
    okPressed = true;
    okPressStart = now;
  }
  last = now;
}

volatile bool upPressed = false;
volatile uint32_t upPressStart = 0;
void IRAM_ATTR handleUPInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();

  if (now - last > 50)
  {
    upPressed = true;
    upPressStart = now;
  }
  last = now;
}

volatile bool downPressed = false;
volatile uint32_t downPressStart = 0;
void IRAM_ATTR handleDownInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();

  if (now - last > 50)
  {
    downPressed = true;
    downPressStart = now;
  }
  last = now;
}

volatile bool rightPressed = false;
volatile uint32_t rightPressStart = 0;
void IRAM_ATTR handleRightInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();

  if (now - last > 50)
  {
    rightPressed = true;
    rightPressStart = now;
  }
  last = now;
}

volatile bool leftPressed = false;
volatile uint32_t leftPressStart = 0;
void IRAM_ATTR handleLeftInterrupt()
{
  static uint32_t last = 0;
  uint32_t now = millis();

  if (now - last > 50)
  {
    leftPressed = true;
    leftPressStart = now;
  }
  last = now;
}

// ===== START MODE =====
typedef enum
{
  STATE_NEUTRAL,
  STATE_CREATURE,
  STATE_INSTANT,
  STATE_SORCERY,
  STATE_ENCHANTMENT,
  STATE_ARTIFACT,
  STATE_PLANESWALKER,
  STATE_SENDING_CARD,
  STATE_CONFIRM_MANA_SHUFFLE,
  STATE_LAND,
  STATE_UNKNOWN
} State_t;

// ====== Servo Stuff ======
#define SERVO_STANDARD 0
#define SERVO_CONTINUOUS 1

const int NUM_SERVOS = 6;
const int servoPins[NUM_SERVOS] = {1, 2, 3, 4, 5, 6};

// If you want standard vs continuous behavior:
const int servoTypes[NUM_SERVOS] = {
    SERVO_STANDARD,   // 0
    SERVO_CONTINUOUS, // 1
    SERVO_CONTINUOUS, // 2
    SERVO_CONTINUOUS, // 3
    SERVO_CONTINUOUS, // 4
    SERVO_CONTINUOUS  // 5
};

// Shared LEDC PWM channel
const int SHARED_PWM_CHANNEL = 0;
int currentActiveServo = -1;

// PWM timing
const int pwmFreq = 50;       // 50 Hz
const int pwmResolution = 14; // 14-bit
const int period_us = 1000000 / pwmFreq;

// ===== SETUP =====
void setup()
{
  Serial.begin(115200);
  Serial0.begin(115200, SERIAL_8N1, 44, 43);

  pinMode(BTN_OK, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(BTN_UP), handleUPInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_DOWN), handleDownInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_RIGHT), handleRightInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_LEFT), handleLeftInterrupt, FALLING);
  attachInterrupt(digitalPinToInterrupt(BTN_OK), handleOKInterrupt, FALLING);

  delay(2000);
  screenInit();
  ledcSetup(SHARED_PWM_CHANNEL, pwmFreq, pwmResolution);
  ledcAttachPin(servoPins[0], SHARED_PWM_CHANNEL);
  ledcWrite(SHARED_PWM_CHANNEL, 0); // or neutral duty
  ledcDetachPin(servoPins[0]);
  currentActiveServo = -1;

  while (true)
  {
    int mode = showMenu();
    if (mode == 0)
    {
      if (!connectWiFi())
      {
        Serial.println("[FATAL] WiFi failed. Rebooting...");
        delay(2000);
        ESP.restart();
      }
      runStartMode();
    }
    else if (mode == 1)
    {
      runTestingMode();
    }
    else if (mode == 2)
    {
      runCalibrateMode();
    }
  }
}

void loop() {}

// ===== FUNCTIONS =====

void getTypeLine()
{
  normalized = "";
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

      WiFiClientSecure client2;
      client2.setInsecure();
      HTTPClient http2;
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
  bool hasLand = raw.indexOf("land") != -1;
  bool hasArtifact = raw.indexOf("artifact") != -1;

  // if a card is both an artifact and a land, prefer Land
  if (hasLand && hasArtifact)
    return "Land";

  if (raw.indexOf("creature") != -1)
    return "Creature";
  if (raw.indexOf("planeswalker") != -1)
    return "Planeswalker";
  if (raw.indexOf("battle") != -1)
    return "Battle";
  if (raw.indexOf("kindred") != -1)
    return "Kindred";
  if (raw.indexOf("instant") != -1)
    return "Instant";
  if (raw.indexOf("sorcery") != -1)
    return "Sorcery";
  if (raw.indexOf("enchantment") != -1)
    return "Enchantment";
  if (hasArtifact)
    return "Artifact";
  if (hasLand)
    return "Land";
  return "Unknown";
}

bool connectWiFi()
{
  currentStatus = "Connecting WiFi...";
  updateEveScreen(); // <-- show status before blocking
  Serial0.println("[WiFi] Connecting...");
  WiFi.begin(ssid, password);
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(300);
    Serial0.print(".");
    if (millis() - start > 15000)
    {
      currentStatus = "WiFi timeout!";
      updateEveScreen();
      Serial0.println("\n[WiFi] Connection timeout");
      return false;
    }
  }
  currentStatus = "WiFi connected";
  updateEveScreen();
  Serial0.print("\n[WiFi] Connected! IP: ");
  Serial0.println(WiFi.localIP());
  return true;
}

void updateEveScreen()
{
  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(0x000000);

  // Title
  EVE_cmd_text(EVE_HSIZE / 2, 8, 26, EVE_OPT_CENTER, "MTG SORT COUNTS");

  // Left column (y starts at 35, step 24)
  char buffer[64];
  sprintf(buffer, "Creatures: %d", CreatureCounter);
  EVE_cmd_text(10, 35, 20, 0, buffer);
  sprintf(buffer, "Instants: %d", InstantCounter);
  EVE_cmd_text(10, 59, 20, 0, buffer);
  sprintf(buffer, "Sorceries: %d", SorceryCounter);
  EVE_cmd_text(10, 83, 20, 0, buffer);
  sprintf(buffer, "Enchantments: %d", EnchantmentCounter);
  EVE_cmd_text(10, 107, 20, 0, buffer);

  // Right column
  sprintf(buffer, "Artifacts: %d", ArtifactCounter);
  EVE_cmd_text(250, 35, 20, 0, buffer);
  sprintf(buffer, "Planeswalkers: %d", OthersCounter);
  EVE_cmd_text(250, 59, 20, 0, buffer);
  sprintf(buffer, "Lands: %d", LandCounter);
  EVE_cmd_text(250, 83, 20, 0, buffer);
  sprintf(buffer, "Unknown: %d", UnknownCounter);
  EVE_cmd_text(250, 107, 20, 0, buffer);

  // Divider
  EVE_cmd_dl(DL_BEGIN | EVE_LINES);
  EVE_cmd_dl(VERTEX2F(10 * 16, 138 * 16));
  EVE_cmd_dl(VERTEX2F(470 * 16, 138 * 16));
  EVE_cmd_dl(DL_END);

  // Last card
  sprintf(buffer, "Last: %s", lastCardType.c_str());
  EVE_cmd_text(10, 148, 20, 0, buffer);

  // Status
  sprintf(buffer, "Status: %s", currentStatus.c_str());
  EVE_cmd_text(10, 172, 20, 0, buffer);

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
  SPI.begin(EVE_SCK, EVE_MISO, EVE_MOSI);
#else
  SPI.begin();
  SPI.beginTransaction(SPISettings(8UL * 1000000UL, MSBFIRST, SPI_MODE0));
#endif

  if (E_OK == EVE_init())
  {
    EVE_cmd_dl(CMD_DLSTART);
    EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
    EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
    EVE_color_rgb(0x000000);
    EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2, 30, EVE_OPT_CENTER, "HELLO WORLD!");
    EVE_cmd_dl(DL_DISPLAY);
    EVE_cmd_dl(CMD_SWAP);
  }
}

void resetCounters()
{
  CreatureCounter = InstantCounter = SorceryCounter = EnchantmentCounter = 0;
  ArtifactCounter = OthersCounter = LandCounter = UnknownCounter = 0;
  lastCardType = "---";
  currentStatus = "Counters reset";
  Serial.println("=== COUNTERS RESET ===");
  updateEveScreen();
}

bool runStartMode()
{
  updateEveScreen();
  State_t currentState = STATE_SENDING_CARD;
  while (true)
  {
    if (currentState == STATE_NEUTRAL && checkMenuHold())
      return true;

    switch (currentState)
    {
    case STATE_SENDING_CARD:
      currentStatus = "Sending next card...";
      updateEveScreen();
      delay(150);
      servoToDegrees(0, 100);
      delay(150);
      GoForward(3, 2500, 500);
      GoForward(2, 2500, 1000);
      GoForward(1, 2500, 2500);
      Stop(1, 1);
      Stop(2, 1);
      Stop(3, 1);
      servoToDegrees(0, 100);
      for (int i = 0; i < 10; i++)
      {
        servoToDegrees(0, 105);
        delay(25);
        servoToDegrees(0, 95);
        delay(25);
      }
      servoToDegrees(0, 100);
      delay(25);
      currentState = STATE_NEUTRAL;
      break;

    case STATE_NEUTRAL:
      if (downPressed)
      {
        downPressed = false;
        currentState = STATE_CONFIRM_MANA_SHUFFLE;
        break;
      }
      currentStatus = "Reading card...";
      updateEveScreen();
      Serial.println("\n=== WAITING FOR CARD ===");
      getTypeLine();
      if (upPressed)
      {
        upPressed = false;
        currentState = STATE_SENDING_CARD;
      }
      if (normalized == "" || normalized == "Unknown")
      {
        Scan_Attempts++;
        if (Scan_Attempts >= 100)
        {
          Serial.println("Read failed 10 times. Sending to Unknown.");
          Scan_Attempts = 0;
          normalized = "Unknown";
          currentState = STATE_UNKNOWN;
        }
        else
        {
          Serial.println("No valid card yet, retrying... (" + String(Scan_Attempts) + "/10)");
          delay(750);
        }
        break;
      }
      Serial.println("Got type: " + normalized);
      Scan_Attempts = 0;
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

    case STATE_CREATURE:
      currentStatus = "Sorting Creature...";
      lastCardType = "Creature";
      updateEveScreen();
      GoForward(5, 1650, 480);
      Stop(5, 1);
      delay(500);
      servoToDegrees(0, 78);
      delay(100);
      servoToDegrees(0, 90);
      delay(100);
      servoToDegrees(0, 80);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 90);
        delay(25);
        servoToDegrees(0, 80);
        delay(25);
      }
      delay(500);
      processCardType("Creature");
      updateEveScreen();
      GoForward(5, 1350, 465);
      Stop(5, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_INSTANT:
      currentStatus = "Sorting Instant...";
      lastCardType = "Instant";
      updateEveScreen();
      GoForward(5, 1650, 1560);
      Stop(5, 1);
      delay(500);
      servoToDegrees(0, 78);
      delay(100);
      servoToDegrees(0, 90);
      delay(100);
      servoToDegrees(0, 80);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 90);
        delay(25);
        servoToDegrees(0, 80);
        delay(25);
      }
      delay(500);
      processCardType("Instant");
      updateEveScreen();
      GoForward(5, 1350, 1373);
      Stop(5, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_SORCERY:
      currentStatus = "Sorting Sorcery...";
      lastCardType = "Sorcery";
      updateEveScreen();
      GoForward(4, 1650, 1595);
      Stop(4, 1);
      delay(500);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      processCardType("Sorcery");
      updateEveScreen();
      GoForward(4, 1350, 1475);
      Stop(4, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_ENCHANTMENT:
      currentStatus = "Sorting Enchantment...";
      lastCardType = "Enchantment";
      updateEveScreen();
      GoForward(4, 1650, 560);
      Stop(4, 1);
      delay(500);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      processCardType("Enchantment");
      updateEveScreen();
      GoForward(4, 1350, 520);
      Stop(4, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_ARTIFACT:
      currentStatus = "Sorting Artifact...";
      lastCardType = "Artifact";
      updateEveScreen();
      GoForward(4, 1350, 1370);
      Stop(4, 1);
      delay(500);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      processCardType("Artifact");
      updateEveScreen();
      GoForward(4, 1650, 1510);
      Stop(4, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_LAND:
      currentStatus = "Sorting Land...";
      lastCardType = "Land";
      updateEveScreen();
      GoForward(4, 1350, 460);
      Stop(4, 1);
      delay(500);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      processCardType("Land");
      updateEveScreen();
      GoForward(4, 1650, 495);
      Stop(4, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_CONFIRM_MANA_SHUFFLE:
      currentStatus = "Mana shuffle?";
      if (downPressed)
      {
        downPressed = false;
        currentState = STATE_CONFIRM_MANA_SHUFFLE; // no-op if already here
      }
      if (upPressed || leftPressed || rightPressed)
      {
        // We don't use these in the main flow here
        upPressed = leftPressed = rightPressed = false;
      }
      // Use the prompt helper to handle selection and actions
      {
        static int selection = 0; // 0 = Yes, 1 = No
        if (digitalRead(BTN_LEFT) == LOW || digitalRead(BTN_UP) == LOW)
        {
          selection = 0;
          delay(150);
        }
        if (digitalRead(BTN_RIGHT) == LOW || digitalRead(BTN_DOWN) == LOW)
        {
          selection = 1;
          delay(150);
        }
        drawManaShufflePrompt(selection);
        if (okPressed)
        {
          int hold = waitOKRelease();
          if (hold == 2)
            return true;
          if (selection == 0)
          {
            okPressed = false;
            return runManaShuffleMode();
          }
          resetCounters();
          return true;
        }
      }
      delay(100);
      break;

    case STATE_PLANESWALKER:
      currentStatus = "Sorting Planeswalker...";
      lastCardType = "Planeswalker";
      updateEveScreen();
      GoForward(5, 1350, 1495);
      Stop(5, 1);
      delay(500);
      servoToDegrees(0, 78);
      delay(100);
      servoToDegrees(0, 90);
      delay(100);
      servoToDegrees(0, 80);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 90);
        delay(25);
        servoToDegrees(0, 80);
        delay(25);
      }
      delay(500);
      processCardType("Planeswalker");
      updateEveScreen();
      GoForward(5, 1650, 1635);
      Stop(5, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_UNKNOWN:
      currentStatus = "Sorting Unknown...";
      lastCardType = "Unknown";
      updateEveScreen();
      GoForward(5, 1350, 540);
      Stop(5, 1);
      delay(500);
      servoToDegrees(0, 78);
      delay(100);
      servoToDegrees(0, 90);
      delay(100);
      servoToDegrees(0, 80);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 90);
        delay(25);
        servoToDegrees(0, 80);
        delay(25);
      }
      delay(500);
      processCardType("Unknown");
      updateEveScreen();
      GoForward(5, 1650, 580);
      Stop(5, 1);
      currentState = STATE_SENDING_CARD;
      break;
    }
  }
}

/* bool runStartMode()
{
  updateEveScreen();
  State_t currentState = STATE_SENDING_CARD;
  while (true)
  {
    if (currentState == STATE_NEUTRAL && checkMenuHold())
      return true;

    switch (currentState)
    {
    case STATE_SENDING_CARD:
      // Match testing behavior: small delays around initial servo position
      currentStatus = "Sending next card...";
      updateEveScreen();
      delay(150);
      servoToDegrees(0, 100);
      delay(150);
      GoForward(3, 2500, 500);
      GoForward(2, 2500, 1000);
      GoForward(1, 2500, 2000);
      Stop(1, 1);
      Stop(2, 1);
      Stop(3, 1);
      for (int i = 0; i < 30; i++)
      {
        servoToDegrees(0, 105);
        delay(25);
        servoToDegrees(0, 95);
        delay(25);
      }
      servoToDegrees(0, 100);
      currentState = STATE_NEUTRAL;
      break;
    case STATE_NEUTRAL:
      currentStatus = "Reading card...";
      updateEveScreen();
      Serial.println("\n=== WAITING FOR CARD ===");
      getTypeLine();
      if (upPressed)
      {
        upPressed = false;
        currentState = STATE_SENDING_CARD;
      }
      if (normalized == "" || normalized == "Unknown")
      {
        Serial.println("No valid card yet, retrying...");
        Scan_Attempts++;
        delay(750);
        break;
      }

      Serial.println("Got type: " + normalized);
      Scan_Attempts = 0;

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
      currentStatus = "Sorting Land...";
      lastCardType = "Land";
      updateEveScreen();
      GoForward(4, 1650, 700);
      Stop(4, 1);
      delay(100);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      Serial.println("LAND SORTED");
      processCardType("Land");
      updateEveScreen();
      GoForward(4, 1358, 700);
      Stop(4, 1);
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_CREATURE:
      currentStatus = "Sorting Creature...";
      lastCardType = "Creature";
      updateEveScreen();
      GoForward(5, 1650, 590);
      Stop(5, 1);
      delay(100);
      servoToDegrees(0, 78);
      delay(100);
      servoToDegrees(0, 90);
      delay(100);
      servoToDegrees(0, 80);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 90);
        delay(25);
        servoToDegrees(0, 80);
        delay(25);
      }
      delay(500);
      Serial.println("CREATURE SORTED");
      processCardType("Creature");
      updateEveScreen();
      GoForward(5, 1350, 555);
      Stop(5, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_ARTIFACT:
      currentStatus = "Sorting Art...";
      lastCardType = "Artifact";
      updateEveScreen();
      GoForward(4, 1650, 700);
      Stop(4, 1);
      delay(100);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      processCardType("Artifact");
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_ENCHANTMENT:
      currentStatus = "Sorting Enchantment...";
      lastCardType = "Enchantment";
      updateEveScreen();
      processCardType("Enchantment");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_INSTANT:
      currentStatus = "Sorting Instant...";
      lastCardType = "Instant";
      updateEveScreen();
      processCardType("Instant");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_SORCERY:
      currentStatus = "Sorting Sorcery...";
      lastCardType = "Sorcery";
      processCardType("Sorcery");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_PLANESWALKER:
      currentStatus = "Sorting Planeswalker...";
      lastCardType = "Planeswalker";
      processCardType("Planeswalker");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_UNKNOWN:
      currentStatus = "Sorting Unknown...";
      lastCardType = "Unknown";
      processCardType("Unknown");
      currentState = STATE_SENDING_CARD;
      break;
    }
  }
} */

/* bool runTestingMode()
{
  updateEveScreen();
  Serial.println("=== ENTERING TESTING MODE (manual input, start-like sequence) ===");
  Serial.println("Type a card type and press Enter:");
  Serial.println("Valid: creature, instant, sorcery, enchantment, artifact, land, planeswalker, battle, kindred");
  Serial.println("Type 'reset' to reset counters.");
  Serial.println("Hold OK 2s to exit.");

  State_t currentState = STATE_SENDING_CARD;

  while (true)
  {
    // Allow exit back to menu only when in NEUTRAL to match start behavior
    if (currentState == STATE_NEUTRAL && checkMenuHold())
      return true;

    switch (currentState)
    {
    case STATE_SENDING_CARD:
      delay(100);
      servoToDegrees(0, 100);
      delay(100);
      GoForward(3, 2500, 500);
      GoForward(2, 2500, 1000);
      GoForward(1, 2500, 2000);
      Stop(1, 1);
      Stop(2, 1);
      Stop(3, 1);
      currentState = STATE_NEUTRAL;
      break;

    case STATE_NEUTRAL:
      Serial.println("\n=== TESTING: WAITING FOR INPUT ===");
      servoToDegrees(0, 100);
      Serial.print("Enter card type: ");

      // ---- Manual input ----
      {
        String input = "";
        while (true)
        {
          if (Serial.available())
          {
            char c = Serial.read();
            if (c == '\n' || c == '\r')
            {
              if (input.length() > 0)
                break;
            }
            else
            {
              input += c;
            }
          }

          if (checkMenuHold())
            return true;

          delay(10);
        }

        input.trim();
        Serial.println(input);

        if (input.equalsIgnoreCase("reset"))
        {
          resetCounters();
          updateEveScreen();
          break;
        }

        normalized = normalizeType(input);

        if (normalized == "" || normalized == "Unknown")
        {
          Serial.println("TESTING: Unrecognized type, try again.");
          break;
        }

        Serial.println("TESTING: Got type: " + normalized);

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
      }
      break;

    case STATE_LAND:
      GoForward(4, 1650, 700);
      Stop(4, 1);
      delay(100);
      servoToDegrees(0, 122);
      delay(100);
      servoToDegrees(0, 110);
      delay(100);
      servoToDegrees(0, 120);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 110);
        delay(25);
        servoToDegrees(0, 120);
        delay(25);
      }
      delay(500);
      Serial.println("TESTING: LAND SORTED");
      processCardType("Land");
      updateEveScreen();
      GoForward(4, 1358, 700);
      Stop(4, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_CREATURE:
      GoForward(5, 1650, 590);
      Stop(5, 1);
      delay(100);
      servoToDegrees(0, 78);
      delay(100);
      servoToDegrees(0, 90);
      delay(100);
      servoToDegrees(0, 80);
      delay(25);
      for (int i = 0; i < 15; i++)
      {
        servoToDegrees(0, 90);
        delay(25);
        servoToDegrees(0, 80);
        delay(25);
      }
      delay(500);
      Serial.println("TESTING: CREATURE SORTED");
      processCardType("Creature");
      updateEveScreen();
      GoForward(5, 1350, 555);
      Stop(5, 1);
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_ARTIFACT:
      Serial.println("TESTING: ARTIFACT SORTED");
      processCardType("Artifact");
      updateEveScreen();
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_ENCHANTMENT:
      Serial.println("TESTING: ENCHANTMENT SORTED");
      processCardType("Enchantment");
      updateEveScreen();
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_INSTANT:
      Serial.println("TESTING: INSTANT SORTED");
      processCardType("Instant");
      updateEveScreen();
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_SORCERY:
      Serial.println("TESTING: SORCERY SORTED");
      processCardType("Sorcery");
      updateEveScreen();
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_PLANESWALKER:
      Serial.println("TESTING: PLANESWALKER SORTED");
      processCardType("Planeswalker");
      updateEveScreen();
      currentState = STATE_SENDING_CARD;
      break;

    case STATE_UNKNOWN:
      Serial.println("TESTING: UNKNOWN SORTED");
      processCardType("Unknown");
      updateEveScreen();
      currentState = STATE_SENDING_CARD;
      break;
    }
  }
} */

bool runTestingMode()
{
  const char *typeNames[] = {
      "Creature", "Instant", "Sorcery", "Enchantment",
      "Artifact", "Land", "Planeswalker", "Unknown"};
  const int NUM_TYPES = 8;

  int fwdDuration[NUM_TYPES] = {590, 500, 500, 500, 500, 700, 500, 500};
  int retDuration[NUM_TYPES] = {555, 500, 500, 500, 500, 700, 500, 500};
  int servoAngle[NUM_TYPES] = {100, 100, 100, 100, 100, 100, 100, 100};
  int convServo[NUM_TYPES] = {5, 5, 4, 4, 4, 4, 5, 5};
  int fwdSpeed[NUM_TYPES] = {1650, 1650, 1650, 1650, 1350, 1350, 1350, 1350};
  int retSpeed[NUM_TYPES] = {1350, 1350, 1350, 1350, 1650, 1650, 1650, 1650};

  auto drawGrid = [&](int selectedIdx)
  {
    const int COLS = 2;
    const int cellW = 200, cellH = 50, gapX = 20, gapY = 8;
    const int gridW = COLS * cellW + (COLS - 1) * gapX;
    const int startX = (EVE_HSIZE - gridW) / 2;
    const int startY = 10;
    EVE_cmd_dl(CMD_DLSTART);
    EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
    EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
    for (int i = 0; i < NUM_TYPES; i++)
    {
      int x = startX + (i % COLS) * (cellW + gapX);
      int y = startY + (i / COLS) * (cellH + gapY);
      EVE_cmd_dl(DL_COLOR_RGB | (i == selectedIdx ? 0xA855F7 : 0x4B2B7F));
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(0xF5F5DC);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 26, EVE_OPT_CENTER, typeNames[i]);
    }
    EVE_color_rgb(0x333333);
    EVE_cmd_text(EVE_HSIZE / 2, 258, 18, EVE_OPT_CENTER, "OK=select  Hold OK 2s=menu");
    EVE_cmd_dl(DL_DISPLAY);
    EVE_cmd_dl(CMD_SWAP);
  };

  auto drawAdjust = [&](int idx, const char *lastResult)
  {
    char buf[64];
    EVE_cmd_dl(CMD_DLSTART);
    EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
    EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
    EVE_color_rgb(0x4B2B7F);
    EVE_cmd_text(EVE_HSIZE / 2, 14, 28, EVE_OPT_CENTER, typeNames[idx]);
    EVE_color_rgb(0x000000);
    sprintf(buf, "Servo:      %d", convServo[idx]);
    EVE_cmd_text(20, 55, 22, 0, buf);
    sprintf(buf, "Fwd speed:  %d us", fwdSpeed[idx]);
    EVE_cmd_text(20, 82, 22, 0, buf);
    sprintf(buf, "Fwd time:   %d ms", fwdDuration[idx]);
    EVE_cmd_text(20, 109, 22, 0, buf);
    sprintf(buf, "Angle:      %d deg", servoAngle[idx]);
    EVE_cmd_text(20, 136, 22, 0, buf);
    sprintf(buf, "Ret time:   %d ms", retDuration[idx]);
    EVE_cmd_text(20, 163, 22, 0, buf);
    EVE_color_rgb(0x1a7a1a);
    EVE_cmd_text(20, 196, 20, 0, lastResult);
    EVE_color_rgb(0x555555);
    EVE_cmd_text(EVE_HSIZE / 2, 228, 18, EVE_OPT_CENTER, "UP/DN=fwd  LR=ret  OK=run  HoldOK=back");
    EVE_cmd_dl(DL_DISPLAY);
    EVE_cmd_dl(CMD_SWAP);
  };

  // Returns: 0 = short press (run), 1 = medium hold (back to grid), 2 = long hold (menu)
  auto waitOKRelease = [&]() -> int
  {
    uint32_t pressTime = millis();
    while (digitalRead(BTN_OK) == LOW)
    {
      uint32_t held = millis() - pressTime;
      if (held >= MENU_HOLD_MS)
      {
        okPressed = false;
        return 2; // long hold → menu
      }
      delay(10);
    }
    okPressed = false;
    delay(50);
    uint32_t held = millis() - pressTime;
    if (held >= ADJUST_BACK_MS)
      return 1; // medium hold → back to grid
    return 0;   // short press → run
  };

  auto runMotors = [&](int idx)
  {
    int sv = convServo[idx];
    int fsp = fwdSpeed[idx];
    int fdt = fwdDuration[idx];
    int ang = servoAngle[idx];
    int rsp = retSpeed[idx];
    int rdt = retDuration[idx];
    int oscB = (ang < 90) ? ang + 10 : ang - 10;
    GoForward(sv, fsp, fdt);
    Stop(sv, 1);
    delay(100);
    servoToDegrees(0, ang);
    delay(100);
    servoToDegrees(0, oscB);
    delay(100);
    servoToDegrees(0, ang);
    delay(25);
    for (int i = 0; i < 15; i++)
    {
      servoToDegrees(0, oscB);
      delay(25);
      servoToDegrees(0, ang);
      delay(25);
    }
    delay(500);
    GoForward(sv, rsp, rdt);
    Stop(sv, 1);
  };

  int selected = 0;
  const int COLS = 2;
  bool inAdjust = false;
  int adjustIdx = 0;
  char lastResult[48] = "No run yet";

  upPressed = downPressed = leftPressed = rightPressed = okPressed = false;
  currentStatus = "Testing mode";
  drawGrid(selected);
  Serial.println("=== TESTING MODE ===");

  // Repeat-press tracking
  uint32_t upHeldSince = 0;
  uint32_t downHeldSince = 0;
  uint32_t leftHeldSince = 0;
  uint32_t rightHeldSince = 0;
  const uint32_t REPEAT_DELAY = 300; // ms before repeat kicks in
  const uint32_t REPEAT_RATE = 80;   // ms between repeats

  while (true)
  {
    if (!inAdjust)
    {
      // Grid navigation — single-step only, no repeat needed
      bool moved = false;
      if (rightPressed)
      {
        rightPressed = false;
        if (selected % COLS < COLS - 1)
          selected++;
        moved = true;
      }
      if (leftPressed)
      {
        leftPressed = false;
        if (selected % COLS > 0)
          selected--;
        moved = true;
      }
      if (downPressed)
      {
        downPressed = false;
        if (selected + COLS < NUM_TYPES)
          selected += COLS;
        moved = true;
      }
      if (upPressed)
      {
        upPressed = false;
        if (selected - COLS >= 0)
          selected -= COLS;
        moved = true;
      }
      if (moved)
        drawGrid(selected);

      if (okPressed)
      {
        int hold = waitOKRelease();
        if (hold == 2)
          return true; // menu
        // short or medium press → enter adjust
        inAdjust = true;
        adjustIdx = selected;
        snprintf(lastResult, sizeof(lastResult), "No run yet");
        upPressed = downPressed = leftPressed = rightPressed = false;
        upHeldSince = downHeldSince = leftHeldSince = rightHeldSince = 0;
        drawAdjust(adjustIdx, lastResult);
      }
    }
    else
    {
      // ── Adjust screen: UP/DN = fwd time, LEFT/RIGHT = ret time ───────────
      bool redraw = false;
      uint32_t now = millis();

      // UP → fwd time +5
      if (digitalRead(BTN_UP) == LOW)
      {
        if (upHeldSince == 0)
          upHeldSince = now;
        uint32_t held = now - upHeldSince;
        if (upPressed || (held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_RATE < 20))
        {
          upPressed = false;
          fwdDuration[adjustIdx] += 5;
          redraw = true;
        }
      }
      else
      {
        upHeldSince = 0;
        upPressed = false;
      }

      // DOWN → fwd time -5
      if (digitalRead(BTN_DOWN) == LOW)
      {
        if (downHeldSince == 0)
          downHeldSince = now;
        uint32_t held = now - downHeldSince;
        if (downPressed || (held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_RATE < 20))
        {
          downPressed = false;
          if (fwdDuration[adjustIdx] > 5)
            fwdDuration[adjustIdx] -= 5;
          redraw = true;
        }
      }
      else
      {
        downHeldSince = 0;
        downPressed = false;
      }

      // RIGHT → ret time +5
      if (digitalRead(BTN_RIGHT) == LOW)
      {
        if (rightHeldSince == 0)
          rightHeldSince = now;
        uint32_t held = now - rightHeldSince;
        if (rightPressed || (held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_RATE < 20))
        {
          rightPressed = false;
          retDuration[adjustIdx] += 5;
          redraw = true;
        }
      }
      else
      {
        rightHeldSince = 0;
        rightPressed = false;
      }

      // LEFT → ret time -5
      if (digitalRead(BTN_LEFT) == LOW)
      {
        if (leftHeldSince == 0)
          leftHeldSince = now;
        uint32_t held = now - leftHeldSince;
        if (leftPressed || (held >= REPEAT_DELAY && (held - REPEAT_DELAY) % REPEAT_RATE < 20))
        {
          leftPressed = false;
          if (retDuration[adjustIdx] > 5)
            retDuration[adjustIdx] -= 5;
          redraw = true;
        }
      }
      else
      {
        leftHeldSince = 0;
        leftPressed = false;
      }

      if (redraw)
        drawAdjust(adjustIdx, lastResult);

      if (okPressed)
      {
        int hold = waitOKRelease();
        if (hold == 2)
          return true; // long hold → menu
        if (hold == 1) // medium hold → back to grid
        {
          inAdjust = false;
          upHeldSince = downHeldSince = leftHeldSince = rightHeldSince = 0;
          upPressed = downPressed = leftPressed = rightPressed = false;
          drawGrid(selected);
        }
        else // short press → run motors
        {
          currentStatus = String("Sorting ") + typeNames[adjustIdx] + "...";
          lastCardType = typeNames[adjustIdx];
          snprintf(lastResult, sizeof(lastResult), "Running fwd=%dms ret=%dms", fwdDuration[adjustIdx], retDuration[adjustIdx]);
          drawAdjust(adjustIdx, lastResult);
          Serial.printf("TESTING: %s  fwd=%dms  ret=%dms  angle=%d  servo=%d\n",
                        typeNames[adjustIdx], fwdDuration[adjustIdx], retDuration[adjustIdx],
                        servoAngle[adjustIdx], convServo[adjustIdx]);
          runMotors(adjustIdx);
          processCardType(typeNames[adjustIdx]);
          snprintf(lastResult, sizeof(lastResult), "Done. fwd=%dms ret=%dms", fwdDuration[adjustIdx], retDuration[adjustIdx]);
          currentStatus = "Testing mode";
          drawAdjust(adjustIdx, lastResult);
        }
      }
    }

    delay(20);
  }
}

bool runCalibrateMode()
{
  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(0x000000);
  EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2 - 20, 27, EVE_OPT_CENTER, "CALIBRATE MODE");
  EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2 + 20, 22, EVE_OPT_CENTER, "Hold OK 2s for menu");
  EVE_cmd_dl(DL_DISPLAY);
  EVE_cmd_dl(CMD_SWAP);

  while (true)
  {
    if (digitalRead(BTN_LEFT) == LOW)
    {
      Serial.println("LEFT");
      GoForward(5, 1550, 5);
      Stop(5, 1);
    }

    if (digitalRead(BTN_RIGHT) == LOW)
    {
      Serial.println("RIGHT");
      GoForward(5, 1450, 5);
      Stop(5, 1);
    }

    if (digitalRead(BTN_UP) == LOW)
    {
      Serial.println("UP");
      GoForward(4, 1550, 10);
      Stop(4, 1);
    }

    if (digitalRead(BTN_DOWN) == LOW)
    {
      Serial.println("DOWN");
      GoForward(4, 1450, 10);
      Stop(4, 1);
    }
    if (checkMenuHold())
      return true;
  }
}

int waitOKRelease()
{
  uint32_t pressTime = okPressStart;
  while (digitalRead(BTN_OK) == LOW)
  {
    if (millis() - pressTime >= MENU_HOLD_MS)
    {
      okPressed = false;
      return 2;
    }
    delay(10);
  }

  okPressed = false;
  delay(50);
  uint32_t held = millis() - pressTime;
  if (held >= ADJUST_BACK_MS)
    return 1;
  return 0;
}

bool checkMenuHold()
{
  if (!okPressed)
    return false;

  if (digitalRead(BTN_OK) == LOW)
  {
    if (millis() - okPressStart >= MENU_HOLD_MS)
    {
      okPressed = false;
      return true;
    }
  }
  else
  {
    okPressed = false;
  }

  return false;
}

int showMenu()
{
  const int COLS = 2;
  const int ROWS = 2;
  int selected = 0;

  while (digitalRead(BTN_OK) == LOW)
  {
    delay(10);
  }
  delay(50);
  drawMenu(selected);

  while (true)
  {
    if (digitalRead(BTN_LEFT) == LOW)
    {
      int col = selected % COLS;
      if (col > 0)
        selected--;
      drawMenu(selected);
      delay(200);
    }
    if (digitalRead(BTN_RIGHT) == LOW)
    {
      int col = selected % COLS;
      if (col < COLS - 1)
        selected++;
      drawMenu(selected);
      delay(200);
    }
    if (digitalRead(BTN_UP) == LOW)
    {
      if (selected >= COLS)
        selected -= COLS;
      drawMenu(selected);
      delay(200);
    }
    if (digitalRead(BTN_DOWN) == LOW)
    {
      if (selected + COLS < ROWS * COLS)
        selected += COLS;
      drawMenu(selected);
      delay(200);
    }
    if (digitalRead(BTN_OK) == LOW)
    {
      delay(200);
      return selected;
    }
  }
}

void drawMenu(int selectedIndex)
{
  // Grid: [0]Start [1]Testing / [2]Calibrate [3](future)
  const char *options[] = {"Start", "Testing", "Calibrate", ""};
  const int NUM_OPTIONS = 4;
  const int COLS = 2;

  const int cellW = 120;
  const int cellH = 44;
  const int gapX = 20;
  const int gapY = 20;
  const int gridTotalW = COLS * cellW + (COLS - 1) * gapX; // 260
  const int startX = (EVE_HSIZE - gridTotalW) / 2;
  const int startY = 110;

  // Color palette (purple / beige)
  const uint32_t BEIGE_BG = 0xEBDDBA;        // overall background
  const uint32_t CELL_BEIGE = 0xF5F5DC;      // unselected cell
  const uint32_t SELECTED_PURPLE = 0x663399; // selected cell
  const uint32_t TEXT_PURPLE = 0x4B2B7F;     // text for unselected
  const uint32_t TEXT_BEIGE = 0xF5F5DC;      // text for selected (light)

  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | BEIGE_BG);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(TEXT_PURPLE);

  EVE_cmd_text(EVE_HSIZE / 2, 30, 30, EVE_OPT_CENTER, "MTG SORTER");
  EVE_cmd_text(EVE_HSIZE / 2, 68, 24, EVE_OPT_CENTER, "Select Mode:");

  for (int i = 0; i < NUM_OPTIONS; i++)
  {
    int col = i % COLS;
    int row = i / COLS;
    int x = startX + col * (cellW + gapX);
    int y = startY + row * (cellH + gapY);

    bool isFuture = (options[i][0] == '\0');
    bool isSelected = (i == selectedIndex);

    if (isFuture)
    {
      // placeholder: darker beige rectangle with muted text
      EVE_cmd_dl(DL_COLOR_RGB | 0xD3C5A8);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(TEXT_PURPLE);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 20, EVE_OPT_CENTER, "---");
    }
    else if (isSelected)
    {
      // selected: purple rect, light text
      EVE_cmd_dl(DL_COLOR_RGB | SELECTED_PURPLE);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(TEXT_BEIGE);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 27, EVE_OPT_CENTER, options[i]);
    }
    else
    {
      // unselected: beige rect, purple text
      EVE_cmd_dl(DL_COLOR_RGB | CELL_BEIGE);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(TEXT_PURPLE);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 27, EVE_OPT_CENTER, options[i]);
    }

    // reset to purple for any small decorations
    EVE_color_rgb(TEXT_PURPLE);
  }

  // Push hints well below the grid (startY + 2 rows + padding)
  int hintY = startY + 2 * cellH + 1 * gapY + 30;
  // hint text in purple for contrast
  EVE_color_rgb(TEXT_PURPLE);
  EVE_cmd_text(EVE_HSIZE / 2, hintY, 20, EVE_OPT_CENTER, "LEFT/RIGHT  UP/DOWN select");
  EVE_cmd_text(EVE_HSIZE / 2, hintY + 24, 20, EVE_OPT_CENTER, "OK confirm  Hold OK 2s = menu");

  EVE_cmd_dl(DL_DISPLAY);
  EVE_cmd_dl(CMD_SWAP);
}

void drawManaShufflePrompt(int selected)
{
  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(0x000000);

  EVE_cmd_text(EVE_HSIZE / 2, 40, 28, EVE_OPT_CENTER, "MANA SHUFFLE?");
  EVE_cmd_text(EVE_HSIZE / 2, 80, 20, EVE_OPT_CENTER, "Select YES or NO");

  const char *options[] = {"YES", "NO"};
  const int xpos[] = {120, 360};
  for (int i = 0; i < 2; i++)
  {
    if (selected == i)
    {
      EVE_cmd_dl(DL_COLOR_RGB | 0x663399);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F((xpos[i] - 60) * 16, 140 * 16));
      EVE_cmd_dl(VERTEX2F((xpos[i] + 60) * 16, 200 * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(0xf5f5dc);
      EVE_cmd_text(xpos[i], 170, 24, EVE_OPT_CENTER, options[i]);
    }
    else
    {
      EVE_cmd_dl(DL_COLOR_RGB | 0xF5F5DC);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F((xpos[i] - 60) * 16, 140 * 16));
      EVE_cmd_dl(VERTEX2F((xpos[i] + 60) * 16, 200 * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(0x4B2B7F);
      EVE_cmd_text(xpos[i], 170, 24, EVE_OPT_CENTER, options[i]);
    }
    EVE_color_rgb(0x000000);
  }

  EVE_cmd_text(EVE_HSIZE / 2, 230, 18, EVE_OPT_CENTER, "Press OK to confirm");
  EVE_cmd_dl(DL_DISPLAY);
  EVE_cmd_dl(CMD_SWAP);
}

bool runManaShuffleMode()
{
  currentStatus = "Mana shuffle mode";
  updateEveScreen();

  while (true)
  {
    if (checkMenuHold())
      return true;
    delay(50);
  }
}

void processCardType(String type)
{
  if (type == "Creature")
  {
    CreatureCounter++;
    Serial.println("CREATURE SORTED");
  }
  else if (type == "Instant")
  {
    InstantCounter++;
    Serial.println("INSTANT SORTED");
  }
  else if (type == "Sorcery")
  {
    SorceryCounter++;
    Serial.println("SORCERY SORTED");
  }
  else if (type == "Enchantment")
  {
    EnchantmentCounter++;
    Serial.println("ENCHANTMENT SORTED");
  }
  else if (type == "Artifact")
  {
    ArtifactCounter++;
    Serial.println("ARTIFACT SORTED");
  }
  else if (type == "Planeswalker")
  {
    OthersCounter++;
    Serial.println("PLANESWALKER SORTED");
  }
  else if (type == "Land")
  {
    LandCounter++;
    Serial.println("LAND SORTED");
  }
  else
  {
    UnknownCounter++;
    Serial.println("UNKNOWN SORTED");
  }
  updateEveScreen();
}

void writeServoMicroseconds(int servoId, int us)
{
  if (servoId < 0 || servoId >= NUM_SERVOS)
    return;

  // Reattach only if needed
  if (currentActiveServo != servoId)
  {
    if (currentActiveServo >= 0)
      ledcDetachPin(servoPins[currentActiveServo]);

    ledcAttachPin(servoPins[servoId], SHARED_PWM_CHANNEL);
    currentActiveServo = servoId;
  }

  uint32_t maxDuty = (1UL << pwmResolution) - 1UL;
  uint32_t duty = (uint32_t)((uint64_t)us * maxDuty / (uint64_t)period_us);
  ledcWrite(SHARED_PWM_CHANNEL, duty);
}

// ===== BASIC MOVEMENT =====
void GoForward(int servoId, int speed_us, int Time)
{
  writeServoMicroseconds(servoId, speed_us);
  delay(Time);
}

void Stop(int servoId, int Time)
{
  writeServoMicroseconds(servoId, 1500);
  delay(Time);
}

// ===== STANDARD SERVO POSITIONING =====
void servoToDegrees(int servoId, int degrees)
{
  if (servoId < 0 || servoId >= NUM_SERVOS)
    return;
  int us = 500 + (degrees * 2000) / 180;
  writeServoMicroseconds(servoId, us);
}

void rotateServo(int servoId, int degrees, int holdTime)
{
  servoToDegrees(servoId, degrees);
  delay(holdTime);
}

void rotateServoSlow(int servoId, int targetDeg, int speedDelayMs, int holdTimeMs)
{
  static int lastPos[NUM_SERVOS] = {90, 90, 90, 90, 90, 90};
  int current = lastPos[servoId];
  int step = (targetDeg > current) ? 1 : -1;

  while (current != targetDeg)
  {
    current += step;
    servoToDegrees(servoId, current);
    delay(speedDelayMs);
  }

  lastPos[servoId] = current;
  if (holdTimeMs > 0)
    delay(holdTimeMs);
}

// ===== CONTINUOUS SERVO CONTROL =====
void rotateServoContinuous(int servoId, int speed, int durationMs)
{
  if (servoId < 0 || servoId >= NUM_SERVOS)
    return;

  speed = constrain(speed, -255, 255);
  int us = 1500 + (speed * 500) / 255;

  writeServoMicroseconds(servoId, us);
  delay(durationMs);
}

void stopServoContinuous(int servoId)
{
  writeServoMicroseconds(servoId, 1500);
}

// ===== HOME POSITION =====
void moveAllToHome()
{
  for (int i = 0; i < NUM_SERVOS; i++)
  {
    if (servoTypes[i] == SERVO_STANDARD)
      rotateServo(i, 90, 0);
    else
      stopServoContinuous(i);
  }
}

// ===== SORTING SERVO (OPTIONAL) =====
void runSortingServo(int servoIdx, int counter)
{
  if (counter <= 0)
    return;

  uint32_t maxDuty = (1UL << pwmResolution) - 1UL;
  uint32_t duty_run = (uint32_t)((uint64_t)1700 * maxDuty / (uint64_t)period_us);
  uint32_t duty_stop = (uint32_t)((uint64_t)1500 * maxDuty / (uint64_t)period_us);

  // Detach previous
  if (currentActiveServo >= 0)
    ledcDetachPin(servoPins[currentActiveServo]);

  // Attach new
  ledcAttachPin(servoPins[servoIdx], SHARED_PWM_CHANNEL);
  currentActiveServo = servoIdx;

  // Run
  ledcWrite(SHARED_PWM_CHANNEL, duty_run);
  delay(counter * 600);

  // Stop
  ledcWrite(SHARED_PWM_CHANNEL, duty_stop);
  delay(200);

  // Detach
  ledcDetachPin(servoPins[servoIdx]);
  currentActiveServo = -1;
}
