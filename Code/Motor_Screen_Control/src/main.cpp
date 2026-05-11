#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "wifi_setup.h"
#include <Arduino.h>
#include <stdio.h>
#include <SPI.h>
#include <EVE.h>
#include <ESP32Servo.h>

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
void processCardType(String type);
int showMenu();
void initAllServos();
bool checkMenuHold();
void servoStop(int id);
void servoForward(int id, int speed);
void servoBackward(int id, int speed);
void calibrateServo(int id);
bool runCalibrateMode();
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

#define NUM_SERVOS 6
Servo servos[NUM_SERVOS];
int servoPins[NUM_SERVOS] = {
    1, 6, 5, 7, 3, 2};

int stopPulse[NUM_SERVOS] = {
    1500, 1490, 1490, 1490, 1490, 1490};

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
  { // debounce
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
  { // debounce
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
  { // debounce
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
  { // debounce
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
  STATE_LAND,
  STATE_UNKNOWN
} State_t;

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
  initAllServos();
  // Main program loop — menu re-appears when hold-to-menu triggers
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
      runStartMode(); // returns true when hold-to-menu triggered
    }
    else if (mode == 1)
    {
      runTestingMode(); // returns true when hold-to-menu triggered
    }
    else if (mode == 2)
    {
      runCalibrateMode();
    }
    // Falls back to top of while(true) → showMenu() again
  }
}

void loop()
{
  // Intentionally empty
}

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
  if (raw.indexOf("artifact") != -1)
    return "Artifact";
  if (raw.indexOf("land") != -1)
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

void initAllServos()
{
  for (int i = 0; i < NUM_SERVOS; i++)
  {
    servos[i].attach(servoPins[i], 500, 2500);
    servos[i].writeMicroseconds(stopPulse[i]);
  }
}

// Returns true if the user wants to go back to menu
bool runStartMode()
{
  State_t currentState = STATE_NEUTRAL;

  while (true)
  {

    // Check for hold-to-menu on BTN_OK at neutral state
    if (currentState == STATE_NEUTRAL && checkMenuHold())
    {
      return true; // back to menu
    }

    switch (currentState)
    {
    case STATE_NEUTRAL:
      Serial.println("\n=== WAITING FOR CARD ===");
      getTypeLine();

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
      processCardType("Land");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_CREATURE:
      processCardType("Creature");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_ARTIFACT:
      processCardType("Artifact");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_ENCHANTMENT:
      processCardType("Enchantment");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_INSTANT:
      processCardType("Instant");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_SORCERY:
      processCardType("Sorcery");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_PLANESWALKER:
      processCardType("Planeswalker");
      currentState = STATE_NEUTRAL;
      break;
    case STATE_UNKNOWN:
      processCardType("Unknown");
      currentState = STATE_NEUTRAL;
      break;
    }
  }
}

// ===== TESTING MODE (stub) =====
// Returns true when user wants to go back to menu
bool runTestingMode()
{
  // Draw a placeholder screen
  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(0x000000);
  EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2 - 20, 27, EVE_OPT_CENTER, "TESTING MODE");
  EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2 + 20, 22, EVE_OPT_CENTER, "Hold OK 2s for menu");
  EVE_cmd_dl(DL_DISPLAY);
  EVE_cmd_dl(CMD_SWAP);

  while (true)
  {
    if (digitalRead(BTN_OK) == LOW)
    {
      servos[0].writeMicroseconds(1410);
      servos[0].writeMicroseconds(1610);
      delay(100);
      servos[0].writeMicroseconds(1510);
      delay(100);
      servos[0].writeMicroseconds(1610);
      servoForward(1, 100);
      servoForward(2, 100);
      servoForward(3, 100);
      servoForward(4, 100);
      servoForward(5, 100);
      delay(One_Card_Delay);
      servoStop(1);
      servoStop(2);
      servoStop(3);
      servoStop(4);
      servoStop(5);
    }
    if (digitalRead(BTN_DOWN) == LOW)
    {
      One_Card_Delay -= 1;
      Serial.print("One_Card_Delay: ");
      Serial.println(One_Card_Delay);
      delay(200);
    }
    if (digitalRead(BTN_UP) == LOW)
    {
      One_Card_Delay += 1;
      Serial.print("One_Card_Delay: ");
      Serial.println(One_Card_Delay);
      delay(200);
    }

    if (checkMenuHold())
      return true; // back to menu
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
    if (digitalRead(BTN_OK) == LOW)
    {
      servoForward(1, 100);
      servoForward(2, 100);
      servoForward(3, 100);
      servoForward(4, 100);
      servoForward(5, 100);
      delay(One_Card_Delay);
      servoStop(1);
      servoStop(2);
      servoStop(3);
      servoStop(4);
      servoStop(5);
      delay(500);
    }
    else if (digitalRead(BTN_DOWN) == LOW)
    {
      servoForward(1, 100);
      //servoForward(2, 100);
      //servoForward(3, 100);
      //servoForward(4, 100);
      //servoForward(5, 100);
      delay(One_Card_Delay);
      servoStop(1);
      //servoStop(2);
      //servoStop(3);
      //servoStop(4);
      //servoStop(5);
      delay(500);
    }
    else if (digitalRead(BTN_UP) == LOW)
    {
      Serial.println("UP pressed");
      servoBackward(4, 100);
      delay(100);
    }
    else if (leftPressed)
    {
      leftPressed = false;
      Serial.println("LEFT interrupt fired");
      servoBackward(5, 100);
      delay(100);
    }
    else if (rightPressed)
    {
      rightPressed = false;
      Serial.println("RIGHT interrupt fired");
      servoForward(5, 100);
      delay(100);
    }

    if (checkMenuHold())
      return true;
  }
}

// ===== HOLD-TO-MENU HELPER =====
// Returns true if BTN_OK held for MENU_HOLD_MS — call this inside mode loops
bool checkMenuHold()
{
  if (!okPressed)
    return false;

  // Button is still held?
  if (digitalRead(BTN_OK) == LOW)
  {
    if (millis() - okPressStart >= MENU_HOLD_MS)
    {
      okPressed = false;
      return true; // held long enough → return to menu
    }
  }
  else
  {
    // Button was released before 2 seconds
    okPressed = false;
  }

  return false;
}

// ===== MENU =====
int showMenu()
{
  // Grid layout:
  // [0] Start     [1] Testing
  // [2] Calibrate [3] (future)
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
      return selected; // 0=Start, 1=Testing, 2=Calibrate, 3=future
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

  EVE_cmd_dl(CMD_DLSTART);
  EVE_cmd_dl(DL_CLEAR_COLOR_RGB | 0xffffff);
  EVE_cmd_dl(DL_CLEAR | CLR_COL | CLR_STN | CLR_TAG);
  EVE_color_rgb(0x000000);

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
      // dashed placeholder — draw as plain dim rect
      EVE_cmd_dl(DL_COLOR_RGB | 0xcccccc);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(0xaaaaaa);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 20, EVE_OPT_CENTER, "---");
    }
    else if (isSelected)
    {
      EVE_cmd_dl(DL_COLOR_RGB | 0x2255CC);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(0xffffff);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 27, EVE_OPT_CENTER, options[i]);
    }
    else
    {
      EVE_cmd_dl(DL_COLOR_RGB | 0xeeeeee);
      EVE_cmd_dl(DL_BEGIN | EVE_RECTS);
      EVE_cmd_dl(VERTEX2F(x * 16, y * 16));
      EVE_cmd_dl(VERTEX2F((x + cellW) * 16, (y + cellH) * 16));
      EVE_cmd_dl(DL_END);
      EVE_color_rgb(0x000000);
      EVE_cmd_text(x + cellW / 2, y + cellH / 2, 27, EVE_OPT_CENTER, options[i]);
    }

    EVE_color_rgb(0x000000);
  }

  // Push hints well below the grid (startY + 2 rows + padding)
  int hintY = startY + 2 * cellH + 1 * gapY + 30;
  EVE_cmd_text(EVE_HSIZE / 2, hintY, 20, EVE_OPT_CENTER, "LEFT/RIGHT  UP/DOWN select");
  EVE_cmd_text(EVE_HSIZE / 2, hintY + 24, 20, EVE_OPT_CENTER, "OK confirm  Hold OK 2s = menu");

  EVE_cmd_dl(DL_DISPLAY);
  EVE_cmd_dl(CMD_SWAP);
}

// ===== SHARED CARD PROCESSING =====
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

void servoStop(int id)
{
  servos[id].writeMicroseconds(stopPulse[id]);
}

void servoForward(int id, int speed)
{
  speed = constrain(speed, 0, 100);
  int pulse = stopPulse[id] + map(speed, 0, 100, 0, 400);
  servos[id].writeMicroseconds(pulse);
}

void servoBackward(int id, int speed)
{
  speed = constrain(speed, 0, 100);
  int pulse = stopPulse[id] - map(speed, 0, 100, 0, 400);
  servos[id].writeMicroseconds(pulse);
}

void calibrateServo(int id)
{
  Serial.println();
  Serial.printf("=== Calibrating Servo %d ===\n", id);
  Serial.println("Watch the servo and find the pulse where it stops moving.");
  Serial.println("Use that value as stopPulse[id].");
  Serial.println("----------------------------------------");

  for (int us = 1450; us <= 1550; us += 5)
  {
    servos[id].writeMicroseconds(us);
    Serial.printf("Pulse: %d µs\n", us);
    delay(1500);
  }

  servos[id].writeMicroseconds(stopPulse[id]); // return to neutral
  Serial.println("Calibration sweep complete.");
  Serial.println("----------------------------------------");
}
