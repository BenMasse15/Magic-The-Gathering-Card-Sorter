#pragma once
#include <ESP32Servo.h>

#define NUM_SERVOS 6
#define SERVO_UPDATE_INTERVAL 20   // 50 Hz

class ServoManager {
public:
    Servo servos[NUM_SERVOS];
    int pins[NUM_SERVOS];
    int stopPulse[NUM_SERVOS];
    int targetPulse[NUM_SERVOS];

    uint32_t lastUpdate = 0;

    ServoManager(int pinList[NUM_SERVOS], int stopList[NUM_SERVOS]) {
        for (int i = 0; i < NUM_SERVOS; i++) {
            pins[i] = pinList[i];
            stopPulse[i] = stopList[i];
            targetPulse[i] = stopList[i];
        }
    }

    void begin() {
        for (int i = 0; i < NUM_SERVOS; i++) {
            servos[i].attach(pins[i], 500, 2500);
            servos[i].writeMicroseconds(stopPulse[i]);
        }
    }

    void update() {
        uint32_t now = millis();
        if (now - lastUpdate < SERVO_UPDATE_INTERVAL) return;
        lastUpdate = now;

        for (int i = 0; i < NUM_SERVOS; i++) {
            servos[i].writeMicroseconds(targetPulse[i]);
        }
    }

    void stop(int id) {
        targetPulse[id] = stopPulse[id];
    }

    void forward(int id, int speed) {
        speed = constrain(speed, 0, 100);
        targetPulse[id] = stopPulse[id] + map(speed, 0, 100, 0, 400);
    }

    void backward(int id, int speed) {
        speed = constrain(speed, 0, 100);
        targetPulse[id] = stopPulse[id] - map(speed, 0, 100, 0, 400);
    }

    void stopAll() {
        for (int i = 0; i < NUM_SERVOS; i++)
            targetPulse[i] = stopPulse[i];
    }
};
