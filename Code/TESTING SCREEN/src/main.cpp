#include <Arduino.h>
#include <stdio.h>
#include <SPI.h>
#include <EVE.h>

void getTypeLineManual();
String normalizeType(String raw);
void writeServoMicroseconds(int servoId, int us);
void GoForward(int servoId, int speed_us, int Time);
void Stop(int servoId, int Time);
void rotateServo(int servoId, int degrees, int holdTime);
void rotateServoSlow(int servoId, int targetDeg, int speedDelayMs, int holdTimeMs);
void servoToDegrees(int servoId, int degrees);
void rotateServoContinuous(int servoId, int speed, int durationMs);
void stopServoContinuous(int servoId);
void moveAllToHome();
void updateEveScreen();
void screenInit();
void resetCounters();
void runSortingServo(int servoIdx, int counter);

String normalized;

// ===== SERVO TYPE CONFIGURATION =====
#define SERVO_STANDARD 0
#define SERVO_CONTINUOUS 1

// ===== SCREEN PINS CONFIGURATION =====
#define EVE_SCK 13
#define EVE_MISO 14
#define EVE_MOSI 21
#define EVE_CS 47
#define EVE_PDN 45

// Index: 0    1    2    3    4    5    6    7    8
// GPIO:  1    2    3    4    5    6    7    8    9
// Role: std  std  Land Crt  Art  Ench Inst Sorc PW/Other
const int NUM_SERVOS = 9;
const int servoPins[NUM_SERVOS] = {1, 2, 3, 4, 5, 6, 7, 8, 9};
const int servoTypes[NUM_SERVOS] = {
    SERVO_STANDARD,   // 0 - GPIO 1
    SERVO_STANDARD,   // 1 - GPIO 2
    SERVO_CONTINUOUS, // 2 - GPIO 3 - Land
    SERVO_CONTINUOUS, // 3 - GPIO 4 - Creature
    SERVO_CONTINUOUS, // 4 - GPIO 5 - Artifact
    SERVO_CONTINUOUS, // 5 - GPIO 6 - Enchantment
    SERVO_CONTINUOUS, // 6 - GPIO 7 - Instant
    SERVO_CONTINUOUS, // 7 - GPIO 8 - Sorcery
    SERVO_CONTINUOUS  // 8 - GPIO 9 - Planeswalker/Other
};

const int SHARED_PWM_CHANNEL = 0;
int currentActiveServo = -1;

const int pwmFreq = 50;
const int pwmResolution = 14;
const int period_us = 1000000 / pwmFreq;

int ArtifactCounter = 0;
int EnchantmentCounter = 0;
int OthersCounter = 0;
int LandCounter = 0;
int SorceryCounter = 0;
int InstantCounter = 0;
int CreatureCounter = 0;
int UnknownCounter = 0;

volatile bool resetRequested = false;
void IRAM_ATTR handleResetInterrupt()
{
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last > 200)
        resetRequested = true;
    last = now;
}

volatile bool ManaShuffle = false;
void IRAM_ATTR handleManaShuffleInterrupt()
{
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last > 200)
        ManaShuffle = true;
    last = now;
}

volatile bool Sorting = false;
void IRAM_ATTR handleSortingInterrupt()
{
    static uint32_t last = 0;
    uint32_t now = millis();
    if (now - last > 200)
        Sorting = true;
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

    Serial.println("=== MTG Card Sorter - Manual Input Mode ===");
    Serial.println("Type a card type and press Enter:");
    Serial.println("  creature, instant, sorcery, enchantment,");
    Serial.println("  artifact, land, planeswalker, battle, kindred");
    Serial.println("  Type 'reset' to reset counters.");
    Serial.println("===========================================");

    updateEveScreen();
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

void loop()
{
    State_t currentState = STATE_NEUTRAL;

    while (true)
    {
        switch (currentState)
        {

        case STATE_NEUTRAL:
            // Stop all continuous servos
            writeServoMicroseconds(2, 1500);
            writeServoMicroseconds(3, 1500);
            writeServoMicroseconds(4, 1500);
            writeServoMicroseconds(5, 1500);
            writeServoMicroseconds(6, 1500);
            writeServoMicroseconds(7, 1500);
            writeServoMicroseconds(8, 1500);

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
                currentActiveServo = -1; 
                runSortingServo(2, LandCounter);
                runSortingServo(3, CreatureCounter);
                runSortingServo(4, ArtifactCounter);
                runSortingServo(5, EnchantmentCounter);
                runSortingServo(6, InstantCounter);
                runSortingServo(7, SorceryCounter);
                runSortingServo(8, OthersCounter);
                currentActiveServo = -1;
                Serial.println("=== Sorting Complete ===");
                Sorting = false;
                break;
            }

            Serial.println("\n=== WAITING FOR INPUT ===");
            Serial.print("Enter card type: ");
            getTypeLineManual();

            if (resetRequested || ManaShuffle || Sorting)
            {
                break;
            }

            if (normalized == "" || normalized == "Unknown")
            {
                Serial.println("Unrecognized type, try again.");
                break;
            }

            Serial.println("Got type: " + normalized);

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
            else if (normalized == "Planeswalker" ||
                     normalized == "Battle" ||
                     normalized == "Kindred")
                currentState = STATE_PLANESWALKER;
            else if (normalized == "Land")
                currentState = STATE_LAND;
            else
                currentState = STATE_UNKNOWN;
            break;

        case STATE_LAND:
            LandCounter++;
            updateEveScreen();
            rotateServoSlow(0, 135, 30, 500);
            rotateServoSlow(1, 40, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            rotateServoSlow(0, 0, 30, 0);

            currentState = STATE_NEUTRAL;
            Serial.println("LAND SORTED");
            break;

        case STATE_CREATURE:
            CreatureCounter++;
            updateEveScreen();
            rotateServoSlow(1, 130, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            currentState = STATE_NEUTRAL;
            Serial.println("CREATURE SORTED");
            break;

        case STATE_ARTIFACT:
            ArtifactCounter++;
            updateEveScreen();
            rotateServoSlow(0, 45, 30, 500);
            rotateServoSlow(1, 130, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            rotateServoSlow(0, 0, 30, 0);
            currentState = STATE_NEUTRAL;
            Serial.println("ARTIFACT SORTED");
            break;

        case STATE_ENCHANTMENT:
            EnchantmentCounter++;
            updateEveScreen();
            rotateServoSlow(0, 90, 30, 500);
            rotateServoSlow(1, 130, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            rotateServoSlow(0, 0, 30, 0);
            currentState = STATE_NEUTRAL;
            Serial.println("ENCHANTMENT SORTED");
            break;

        case STATE_INSTANT:
            InstantCounter++;
            updateEveScreen();
            rotateServoSlow(0, 135, 30, 500);
            rotateServoSlow(1, 130, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            rotateServoSlow(0, 0, 30, 0);
            currentState = STATE_NEUTRAL;
            Serial.println("INSTANT SORTED");
            break;

        case STATE_SORCERY:
            SorceryCounter++;
            updateEveScreen();
            rotateServoSlow(1, 40, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            currentState = STATE_NEUTRAL;
            Serial.println("SORCERY SORTED");
            break;

        case STATE_PLANESWALKER:
            OthersCounter++;
            updateEveScreen();
            rotateServoSlow(0, 45, 30, 500);
            rotateServoSlow(1, 40, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            rotateServoSlow(0, 0, 30, 0);
            currentState = STATE_NEUTRAL;
            Serial.println("PLANESWALKER SORTED");
            break;

        case STATE_UNKNOWN:
            UnknownCounter++;
            updateEveScreen();
            rotateServoSlow(0, 90, 30, 500);
            rotateServoSlow(1, 40, 30, 500);
            rotateServoSlow(1, 90, 30, 500);
            rotateServoSlow(0, 0, 30, 0);
            currentState = STATE_NEUTRAL;
            Serial.println("UNKNOWN SORTED");
            break;
        }
    }
}

// -------------------------------------------------------
// Run one sorting servo for a given card count, then stop
// -------------------------------------------------------
void runSortingServo(int servoIdx, int counter)
{
    if (counter <= 0)
        return;

    uint32_t maxDuty = (1UL << pwmResolution) - 1UL;
    uint32_t duty_run = (uint32_t)((uint64_t)1700 * maxDuty / (uint64_t)period_us);
    uint32_t duty_stop = (uint32_t)((uint64_t)1500 * maxDuty / (uint64_t)period_us);

    // Always detach whatever is current, then attach the target
    if (currentActiveServo >= 0)
        ledcDetachPin(servoPins[currentActiveServo]);

    // Force reattach even if servoIdx == currentActiveServo
    ledcAttachPin(servoPins[servoIdx], SHARED_PWM_CHANNEL);
    currentActiveServo = servoIdx;

    ledcWrite(SHARED_PWM_CHANNEL, duty_run);
    delay(counter * 600);
    ledcWrite(SHARED_PWM_CHANNEL, duty_stop);
    delay(200);

    ledcDetachPin(servoPins[servoIdx]);
    currentActiveServo = -1; // ← always set to -1 after detach

    Serial.println("Servo " + String(servoIdx) + " (GPIO " + String(servoPins[servoIdx]) + ") done.");
}

// -------------------------------------------------------
// Manual input: blocks until the user types a line + Enter
// -------------------------------------------------------
void getTypeLineManual()
{
    normalized = "";
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
        if (resetRequested || ManaShuffle || Sorting)
            return;
    }

    input.trim();
    Serial.println(input);

    if (input.equalsIgnoreCase("reset"))
    {
        resetCounters();
        updateEveScreen();
        return;
    }

    normalized = normalizeType(input);
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

void writeServoMicroseconds(int servoId, int us)
{
    if (servoId < 0 || servoId >= NUM_SERVOS)
        return;
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

void servoToDegrees(int servoId, int degrees)
{
    if (servoId < 0 || servoId >= NUM_SERVOS)
        return;
    int us = 500 + (degrees * 2000) / 180;
    writeServoMicroseconds(servoId, us);
}

void rotateServoContinuous(int servoId, int speed, int durationMs)
{
    if (servoId < 0 || servoId >= NUM_SERVOS)
        return;
    if (speed < -255)
        speed = -255;
    if (speed > 255)
        speed = 255;
    int us = 1500 + (speed * 500) / 255;
    writeServoMicroseconds(servoId, us);
    delay(durationMs);
}

void stopServoContinuous(int servoId)
{
    writeServoMicroseconds(servoId, 1500);
}

void rotateServo(int servoId, int degrees, int holdTime)
{
    servoToDegrees(servoId, degrees);
    delay(holdTime);
}

void rotateServoSlow(int servoId, int targetDeg, int speedDelayMs, int holdTimeMs)
{
    static int lastPos[NUM_SERVOS] = {90, 90, 90, 90, 90, 90, 90, 90, 90};
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
        EVE_cmd_text(EVE_HSIZE / 2, EVE_VSIZE / 2, 30, EVE_OPT_CENTER, "MANUAL INPUT MODE");
        EVE_cmd_dl(DL_DISPLAY);
        EVE_cmd_dl(CMD_SWAP);
    }
}

void resetCounters()
{
    CreatureCounter = EnchantmentCounter = OthersCounter = LandCounter = 0;
    SorceryCounter = InstantCounter = UnknownCounter = ArtifactCounter = 0;
    Serial.println("=== COUNTERS RESET ===");
    updateEveScreen();
}