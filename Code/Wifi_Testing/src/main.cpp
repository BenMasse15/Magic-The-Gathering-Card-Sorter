#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>

// Replace these with your Wi-Fi SSID and password
const char* ssid = "IOT-6220";
const char* password = "6220M@cSelection";
const char* scryfallBase = "https://api.scryfall.com";

String stripLeadingZeros(const String& value) {
  int i = 0;
  while (i < value.length() && value[i] == '0') {
    i++;
  }
  String result = value.substring(i);
  return result.length() ? result : "0";
}

bool isAlphaChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

bool isDigitChar(char c) {
  return c >= '0' && c <= '9';
}

bool isAlphaNumericChar(char c) {
  return isAlphaChar(c) || isDigitChar(c);
}

String normalizeSetNumber(String setCode, String number) {
  setCode.trim();
  number.trim();
  setCode.toLowerCase();

  int write = 0;
  for (int i = 0; i < number.length(); ++i) {
    char c = number[i];
    if (isAlphaNumericChar(c) || c == '-') {
      number[write++] = c;
    }
  }
  number.remove(write);

  int firstDigit = 0;
  while (firstDigit < number.length() && number[firstDigit] == '0') {
    firstDigit++;
  }

  String normalizedNumber = number.substring(firstDigit);
  if (normalizedNumber.length() == 0) {
    normalizedNumber = "0";
  }

  return setCode + "/" + normalizedNumber;
}

String normalizeCodeInput(String code) {
  code.trim();
  if (code.length() == 0) {
    return code;
  }

  code.toLowerCase();

  // Replace multiple whitespace with single spaces and normalize separators.
  String cleaned;
  bool prevSpace = false;
  for (int i = 0; i < code.length(); ++i) {
    char c = code[i];
    if (c == '\t' || c == '\r') {
      c = ' ';
    }
    if (c == ' ') {
      if (prevSpace) {
        continue;
      }
      prevSpace = true;
      cleaned += ' ';
      continue;
    }
    prevSpace = false;
    if (c == '-') {
      cleaned += ' ';
    } else {
      cleaned += c;
    }
  }

  int slashIndex = cleaned.indexOf('/');
  if (slashIndex >= 0) {
    String left = cleaned.substring(0, slashIndex);
    String right = cleaned.substring(slashIndex + 1);
    return normalizeSetNumber(left, right);
  }

  int spaceIndex = cleaned.indexOf(' ');
  if (spaceIndex >= 0) {
    String first = cleaned.substring(0, spaceIndex);
    String second = cleaned.substring(spaceIndex + 1);
    first.trim();
    second.trim();

    bool firstAllDigits = first.length() > 0;
    for (int i = 0; i < first.length(); ++i) {
      if (!isDigitChar(first[i])) {
        firstAllDigits = false;
        break;
      }
    }

    bool secondAllDigits = second.length() > 0;
    for (int i = 0; i < second.length(); ++i) {
      if (!isDigitChar(second[i])) {
        secondAllDigits = false;
        break;
      }
    }

    bool firstIsAlpha = first.length() >= 2;
    for (int i = 0; i < first.length(); ++i) {
      if (!isAlphaNumericChar(first[i])) {
        firstIsAlpha = false;
        break;
      }
    }

    bool secondIsAlpha = second.length() >= 2;
    for (int i = 0; i < second.length(); ++i) {
      if (!isAlphaNumericChar(second[i])) {
        secondIsAlpha = false;
        break;
      }
    }

    if (firstIsAlpha && secondAllDigits) {
      return normalizeSetNumber(first, second);
    }
    if (secondIsAlpha && firstAllDigits) {
      return normalizeSetNumber(second, first);
    }
  }

  int firstDigitIndex = -1;
  for (int i = 0; i < cleaned.length(); ++i) {
    if (isDigitChar(cleaned[i])) {
      firstDigitIndex = i;
      break;
    }
  }

  if (firstDigitIndex > 0 && firstDigitIndex < 7) {
    String prefix = cleaned.substring(0, firstDigitIndex);
    String suffix = cleaned.substring(firstDigitIndex);
    bool prefixAlnum = prefix.length() > 0;
    for (int i = 0; i < prefix.length(); ++i) {
      if (!isAlphaNumericChar(prefix[i])) {
        prefixAlnum = false;
        break;
      }
    }
    if (prefixAlnum) {
      return normalizeSetNumber(prefix, suffix);
    }
  }

  return cleaned;
}

String urlEncode(const String& value) {
  String encoded;
  const char* hex = "0123456789ABCDEF";

  for (int i = 0; i < value.length(); ++i) {
    char c = value[i];
    if (isAlphaNumericChar(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c;
    } else if (c == ' ') {
      encoded += "+";
    } else {
      encoded += '%';
      encoded += hex[(c >> 4) & 0xF];
      encoded += hex[c & 0xF];
    }
  }

  return encoded;
}

bool looksLikeUUID(const String& code) {
  if (code.length() != 36) {
    return false;
  }

  for (int i = 0; i < 36; ++i) {
    char c = code[i];
    if (i == 8 || i == 13 || i == 18 || i == 23) {
      if (c != '-') {
        return false;
      }
    } else if (!isDigitChar(c) && !(c >= 'a' && c <= 'f') && !(c >= 'A' && c <= 'F')) {
      return false;
    }
  }

  return true;
}

String fetchScryfall(const String& url) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient https;
  String fullUrl = String(scryfallBase) + url;

  if (!https.begin(client, fullUrl)) {
    return "ERROR: Failed to begin HTTPS request";
  }

  https.addHeader("User-Agent", "ESP32-Scryfall-Client/1.0");
  https.addHeader("Accept", "application/json");

  int httpCode = https.GET();
  String payload;
  if (httpCode > 0) {
    payload = https.getString();
  } else {
    payload = String("ERROR: HTTP request failed with code ") + httpCode;
  }

  https.end();
  return payload;
}

String fetchCardByCode(String code) {
  String normalized = normalizeCodeInput(code);
  Serial.print("Normalized input: ");
  Serial.println(normalized);

  if (normalized.length() == 0) {
    return "ERROR: empty code";
  }

  if (looksLikeUUID(normalized)) {
    return fetchScryfall(String("/cards/") + normalized);
  }

  bool onlyDigits = true;
  for (int i = 0; i < normalized.length(); ++i) {
    if (!isDigitChar(normalized[i])) {
      onlyDigits = false;
      break;
    }
  }

  if (normalized.indexOf('/') >= 0) {
    String result = fetchScryfall(String("/cards/") + normalized);
    if (!result.startsWith("ERROR") && result.indexOf("\"error\"") < 0) {
      return result;
    }
  }

  if (onlyDigits) {
    String result = fetchScryfall(String("/cards/multiverse/") + normalized);
    if (!result.startsWith("ERROR") && result.indexOf("\"error\"") < 0) {
      return result;
    }
  }

  String exactUrl = String("/cards/named?exact=") + urlEncode(normalized);
  String exactResult = fetchScryfall(exactUrl);
  if (!exactResult.startsWith("ERROR") && exactResult.indexOf("\"error\"") < 0) {
    return exactResult;
  }

  String fuzzyUrl = String("/cards/named?fuzzy=") + urlEncode(normalized);
  return fetchScryfall(fuzzyUrl);
}

void connectWiFi() {
  Serial.print("Connecting to Wi-Fi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  unsigned long startMillis = millis();
  const unsigned long timeout = 20000;

  while (WiFi.status() != WL_CONNECTED && millis() - startMillis < timeout) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("Wi-Fi connected");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Failed to connect to Wi-Fi.");
  }
}

String readSerialLine() {
  String line;
  while (true) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\r') {
        continue;
      }
      if (c == '\n') {
        break;
      }
      line += c;
    }
  }
  line.trim();
  return line;
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println();
  Serial.println("ESP32-S3 Scryfall set+number lookup");
  connectWiFi();
}

void loop() {
  Serial.println();
  Serial.println("Enter MTG code, set/number, or card name:");
  String input = readSerialLine();
  if (input.length() == 0) {
    delay(100);
    return;
  }

  Serial.print("Searching for: ");
  Serial.println(input);
  String response = fetchCardByCode(input);

  Serial.println("=== Scryfall response ===");
  Serial.println(response);
  Serial.println("=========================");
  Serial.println("Ready for next query.");
}
