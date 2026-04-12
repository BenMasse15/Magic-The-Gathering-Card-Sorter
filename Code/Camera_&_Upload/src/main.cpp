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

const int CS_PIN   = 14;
Arducam_Mega cam(CS_PIN);

const size_t MAX_IMAGE_SIZE = 120 * 1024;   // 120 KB buffer
uint8_t imageBuffer[MAX_IMAGE_SIZE];

bool connectWiFi() {
    Serial0.println("[WiFi] Connecting...");

    WiFi.begin(ssid, password);
    uint32_t start = millis();

    while (WiFi.status() != WL_CONNECTED) {
        delay(300);
        Serial0.print(".");
        if (millis() - start > 15000) {
            Serial0.println("\n[WiFi] Connection timeout");
            return false;
        }
    }

    Serial0.print("\n[WiFi] Connected! IP: ");
    Serial0.println(WiFi.localIP());
    return true;
}


int captureImage() {
    Serial0.println("[CAM] Capturing image...");

    cam.takePicture(CAM_IMAGE_MODE_QVGA, CAM_IMAGE_PIX_FMT_JPG);

    while (cam.getReceivedLength() == 0) {
        delay(10);
    }

    size_t length = cam.getTotalLength();
    Serial0.printf("[CAM] Image size reported: %u bytes\n", length);

    if (length > MAX_IMAGE_SIZE) {
        Serial0.println("[CAM] ERROR: Image too large for buffer");
        return -1;
    }

    for (size_t i = 0; i < length; i++) {
        imageBuffer[i] = cam.readByte();
    }

    Serial0.println("[CAM] Image capture complete");
    return length;
}

void sendImageToServer(uint8_t* data, size_t len) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial0.println("[HTTP] WiFi not connected");
        return;
    }

    Serial0.println("[HTTP] Preparing multipart/form-data upload...");

    HTTPClient http;
    http.begin(OCR_URL);

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
    Serial0.printf("[HTTP] Total upload size: %u bytes\n", totalLen);

    uint8_t* body = (uint8_t*)malloc(totalLen);
    if (!body) {
        Serial0.println("[HTTP] ERROR: malloc failed");
        return;
    }

    // Copy header
    memcpy(body, head.c_str(), head.length());

    // Copy image
    memcpy(body + head.length(), data, len);

    // Copy tail
    memcpy(body + head.length() + len, tail.c_str(), tail.length());

    Serial0.println("[HTTP] Sending POST...");
    int httpCode = http.POST(body, totalLen);

    free(body);

    if (httpCode <= 0) {
        Serial0.printf("[HTTP] POST failed: %s\n", http.errorToString(httpCode).c_str());
        return;
    }

    Serial0.printf("[HTTP] Response code: %d\n", httpCode);
    String response = http.getString();

    Serial0.println("----- SERVER RESPONSE BEGIN -----");
    Serial0.println(response);
    Serial0.println("----- SERVER RESPONSE END -------");

    http.end();
}



void setup() {
    Serial0.begin(115200, SERIAL_8N1, 44, 43);
    delay(500);
    Serial0.println("\n=== ESP32-S3 OCR CAMERA CLIENT ===");
    Serial0.println("UART0 is working!");

    if (!connectWiFi()) {
        Serial0.println("[FATAL] WiFi failed. Rebooting...");
        delay(2000);
        ESP.restart();
    }

    Serial0.println("[CAM] Initializing Arducam...");
    cam.begin();
    Serial0.println("[CAM] Ready!");
}

void loop() {
    Serial0.println("\n=== NEW CAPTURE CYCLE ===");

    int imgSize = captureImage();
    if (imgSize > 0) {
        Serial0.printf("[MAIN] Captured %d bytes. Uploading...\n", imgSize);
        sendImageToServer(imageBuffer, imgSize);
    } else {
        Serial0.println("[MAIN] Capture failed");
    }

    delay(3000);
}
