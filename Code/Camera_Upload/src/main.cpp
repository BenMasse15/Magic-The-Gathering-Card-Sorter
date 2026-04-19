#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include "Arducam_Mega.h"
#include "wifi_setup.h"

// -------------------------------
// CONFIGURATION
// -------------------------------
//const char* OCR_URL = "https://ocr-server-ozql.onrender.com/ocr";
const char* OCR_URL = "https://ocr-server-ozql.onrender.com/ocr-and-lookup"; // add /debug-processed for debug endpoint that returns the processed image
const int    HTTP_TIMEOUT_MS = 60000;
const int CS_PIN   = 14;
Arducam_Mega cam(CS_PIN);

const size_t MAX_IMAGE_SIZE = 120 * 1024;   // 120 KB buffer
uint8_t imageBuffer[MAX_IMAGE_SIZE];

bool connectWiFi() {
    Serial.println("[WiFi] Connecting...");

    WiFi.begin(ssid, password);
    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial.print(".");
        if (millis() - start > 15000) {
            Serial.println("\n[WiFi] Connection timeout");
            return false;
        }
    }

    Serial.print("\n[WiFi] Connected! IP: ");
    Serial.println(WiFi.localIP());
    return true;
}


int captureImage() {
    Serial.println("[CAM] Capturing image...");

    cam.takePicture(CAM_IMAGE_MODE_QVGA, CAM_IMAGE_PIX_FMT_JPG);

    while (cam.getReceivedLength() == 0) {
        delay(10);
    }

    size_t length = cam.getTotalLength();
    Serial.printf("[CAM] Image size reported: %u bytes\n", length);

    if (length > MAX_IMAGE_SIZE) {
        Serial.println("[CAM] ERROR: Image too large for buffer");
        return -1;
    }

    for (size_t i = 0; i < length; i++) {
        imageBuffer[i] = cam.readByte();
    }

    Serial.println("[CAM] Image capture complete");
    return length;
}

void sendImageToServer(uint8_t* data, size_t len) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[HTTP] WiFi not connected");
        return;
    }

    Serial.println("[HTTP] Preparing multipart/form-data upload...");

    HTTPClient http;
    http.begin(OCR_URL);
    http.setTimeout(HTTP_TIMEOUT_MS);
    String boundary = "----ESP32Boundary12345";
    String contentType = "multipart/form-data; boundary=" + boundary;
    http.addHeader("Content-Type", contentType);

    // Build multipart header
    String head = "--" + boundary + "\r\n";
    head += "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n";
    head += "Content-Type: image/jpeg\r\n\r\n";

    // Build multipart tail
    String tail = "\r\n--" + boundary + "--\r\n";

    // Allocate a single buffer for the entire POST body
    size_t totalLen = head.length() + len + tail.length();
    Serial.printf("[HTTP] Total upload size: %u bytes\n", totalLen);

    uint8_t* body = (uint8_t*)malloc(totalLen);
    if (!body) {
        Serial.println("[HTTP] ERROR: malloc failed");
        return;
    }

    // Copy header
    memcpy(body, head.c_str(), head.length());

    // Copy image
    memcpy(body + head.length(), data, len);

    // Copy tail
    memcpy(body + head.length() + len, tail.c_str(), tail.length());

    Serial.println("[HTTP] Sending POST...");
    int httpCode = http.POST(body, totalLen);

    free(body);

    if (httpCode <= 0) {
        Serial.printf("[HTTP] POST failed: %s\n", http.errorToString(httpCode).c_str());
        return;
    }

    Serial.printf("[HTTP] Response code: %d\n", httpCode);
    String response = http.getString();

    Serial.println("----- SERVER RESPONSE BEGIN -----");
    Serial.println(response);
    Serial.println("----- SERVER RESPONSE END -------");

    http.end();
}



void setup() {
    Serial.begin(115200);
    delay(500);
    Serial.println("\n=== ESP32-S3 OCR CAMERA CLIENT ===");
    Serial.println("UART0 is working!");

    if (!connectWiFi()) {
        Serial.println("[FATAL] WiFi failed. Rebooting...");
        delay(2000);
        ESP.restart();
    }

    Serial.println("[CAM] Initializing Arducam...");
    cam.begin();
    Serial.println("[CAM] Ready!");
}

void loop() {
    Serial.println("\n=== NEW CAPTURE CYCLE ===");

    int imgSize = captureImage();
    if (imgSize > 0) {
        Serial.printf("[MAIN] Captured %d bytes. Uploading...\n", imgSize);
        sendImageToServer(imageBuffer, imgSize);
    } else {
        Serial.printf("[MAIN] Capture failed\n");
    }

    delay(3000);
}