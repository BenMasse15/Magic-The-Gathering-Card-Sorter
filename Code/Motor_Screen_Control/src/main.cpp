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
bool runStartMode();
bool runTestingMode();
bool runCalibrateMode();
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

#define MENU_HOLD_MS 2000 // hold OK for 2s to return to menu

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

void updateEveScreen()
{
  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(0x000000);
  EVE_cmd_text(EVE_HSIZE / 2, 20, 30, EVE_OPT_CENTER, "MTG SORT COUNTS");

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
      // Match testing behavior: small delays around initial servo position
      delay(150);
      servoToDegrees(0, 100);
      delay(150);
      GoForward(3, 2500, 500);
      GoForward(2, 2500, 1000);
      GoForward(1, 2500, 2000);
      Stop(1, 1);
      Stop(2, 1);
      Stop(3, 1);
      currentState = STATE_NEUTRAL;
      break;
    case STATE_NEUTRAL:
      Serial.println("\n=== WAITING FOR CARD ===");
      getTypeLine();
      if(upPressed) {
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
      // Use the same motor sequence as testing
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
      // Apply testing-mode creature sequence in start mode as well
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
      processCardType("Artifact");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_ENCHANTMENT:
      processCardType("Enchantment");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_INSTANT:
      processCardType("Instant");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_SORCERY:
      processCardType("Sorcery");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_PLANESWALKER:
      processCardType("Planeswalker");
      currentState = STATE_SENDING_CARD;
      break;
    case STATE_UNKNOWN:
      processCardType("Unknown");
      currentState = STATE_SENDING_CARD;
      break;
    }
  }
}

bool runTestingMode()
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
  const uint32_t BEIGE_BG = 0xEBDDBA;     // overall background
  const uint32_t CELL_BEIGE = 0xF5F5DC;   // unselected cell
  const uint32_t SELECTED_PURPLE = 0x663399; // selected cell
  const uint32_t TEXT_PURPLE = 0x4B2B7F;  // text for unselected
  const uint32_t TEXT_BEIGE = 0xF5F5DC;   // text for selected (light)

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
