/*
  ESP8266 + MAX7219 LED matrix grant display

  Libraries:
    - MD_Parola by MajicDesigns
    - MD_MAX72XX by MajicDesigns

  Default NodeMCU / Wemos D1 mini wiring:
    MAX7219 DIN  -> D7 / GPIO13 / MOSI
    MAX7219 CLK  -> D5 / GPIO14 / SCK
    MAX7219 CS   -> D8 / GPIO15
    MAX7219 VCC  -> 5V
    MAX7219 GND  -> GND
*/

#include <Arduino.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WiFi.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <stdio.h>
#include <string.h>

// FC16_HW is the most common 4-in-1 MAX7219 matrix module layout.
// If your text is mirrored, upside down, or scrambled, try PAROLA_HW,
// GENERIC_HW, or ICSTATION_HW.
#define HARDWARE_TYPE MD_MAX72XX::FC16_HW

// Change this to the number of chained 8x8 MAX7219 matrix modules.
// Examples: 1 for one 8x8 module, 4 for one 32x8 module, 8 for two 32x8 modules.
#define MAX_DEVICES 4

const uint8_t CS_PIN = 15;  // D8 / GPIO15 on NodeMCU and Wemos D1 mini.

const uint8_t BRIGHTNESS = 3;          // 0 to 15.
const uint16_t SCROLL_SPEED_MS = 40;   // Lower is faster.
const uint16_t SCROLL_PAUSE_MS = 1000; // Pause after each full scroll.

const char STARTUP_TEXT[] = "noun.wtf";
const unsigned long STARTUP_LETTER_INTERVAL_MS = 350;

const char WIFI_SSID[] = "xxx";
const char WIFI_PASSWORD[] = "xxx";
const char GRANT_API_URL[] = "https://nounv2api.vercel.app/api/grant";

const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long API_REFRESH_INTERVAL_MS = 300000; // 5 minutes.
const unsigned long API_RETRY_INTERVAL_MS = 30000;    // 30 seconds after failure.

MD_Parola matrix = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
BearSSL::WiFiClientSecure secureClient;

char scrollText[128] = "NOUN.WTF";
unsigned long nextGrantFetchAt = 0;

bool timeReached(unsigned long targetTime) {
  return (long)(millis() - targetTime) >= 0;
}

void updateDisplayText(const char *text) {
  strncpy(scrollText, text, sizeof(scrollText) - 1);
  scrollText[sizeof(scrollText) - 1] = '\0';

  matrix.displayClear();
  matrix.displayScroll(scrollText, PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED_MS);

  Serial.print(F("Display text: "));
  Serial.println(scrollText);
}

void displayStaticText(const char *text) {
  matrix.displayClear();
  matrix.setTextAlignment(PA_CENTER);
  matrix.print(text);
}

void serviceStartupText() {
  static uint8_t textIndex = 0;
  static unsigned long lastLetterAt = 0;
  static char letter[2] = "";

  if (lastLetterAt != 0 && millis() - lastLetterAt < STARTUP_LETTER_INTERVAL_MS) {
    return;
  }

  letter[0] = STARTUP_TEXT[textIndex];
  letter[1] = '\0';

  displayStaticText(letter);

  textIndex = (textIndex + 1) % (sizeof(STARTUP_TEXT) - 1);
  lastLetterAt = millis();
}

void serviceDisplay() {
  if (matrix.displayAnimate()) {
    delay(SCROLL_PAUSE_MS);
    matrix.displayReset();
  }
}

void readSerialText() {
  static char inputBuffer[sizeof(scrollText)];
  static uint8_t inputIndex = 0;

  while (Serial.available() > 0) {
    char c = Serial.read();

    if (c == '\r') {
      continue;
    }

    if (c == '\n') {
      inputBuffer[inputIndex] = '\0';

      if (inputIndex > 0) {
        updateDisplayText(inputBuffer);
      }

      inputIndex = 0;
      return;
    }

    if (inputIndex < sizeof(inputBuffer) - 1) {
      inputBuffer[inputIndex++] = c;
    }
  }
}

bool readJsonStringValue(const String &json, const char *key, char *output, size_t outputSize) {
  if (outputSize == 0) return false;

  String quotedKey = String('"') + key + '"';
  int keyPosition = json.indexOf(quotedKey);
  if (keyPosition < 0) return false;

  int colonPosition = json.indexOf(':', keyPosition + quotedKey.length());
  if (colonPosition < 0) return false;

  int valueStart = json.indexOf('"', colonPosition + 1);
  if (valueStart < 0) return false;

  size_t outputIndex = 0;
  bool escaped = false;

  for (int i = valueStart + 1; i < (int)json.length(); i++) {
    char c = json[i];

    if (escaped) {
      switch (c) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'n':
          c = ' ';
          break;
        case 'r':
        case 't':
          c = ' ';
          break;
        default:
          break;
      }
      escaped = false;
    } else if (c == '\\') {
      escaped = true;
      continue;
    } else if (c == '"') {
      output[outputIndex] = '\0';
      return true;
    }

    if (outputIndex < outputSize - 1) {
      output[outputIndex++] = c;
    }
  }

  output[outputIndex] = '\0';
  return false;
}

bool buildGrantMessage(const String &payload, char *output, size_t outputSize) {
  if (outputSize == 0) return false;

  char id[16];
  char title[80];
  char proposer[48];

  if (!readJsonStringValue(payload, "id", id, sizeof(id))) return false;
  if (!readJsonStringValue(payload, "title", title, sizeof(title))) return false;
  if (!readJsonStringValue(payload, "proposer", proposer, sizeof(proposer))) return false;

  snprintf(output, outputSize, "#%s | %s | %s", id, title, proposer);
  output[outputSize - 1] = '\0';
  return true;
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  serviceStartupText();
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("Connecting to WiFi SSID: "));
  Serial.println(WIFI_SSID);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    serviceStartupText();
    delay(50);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print(F("WiFi connected. IP: "));
    Serial.println(WiFi.localIP());
    updateDisplayText("WiFi connected");
    return true;
  }

  Serial.println(F("WiFi connection failed."));
  updateDisplayText("WiFi failed");
  return false;
}

bool fetchGrantData() {
  if (!connectWiFi()) {
    return false;
  }

  updateDisplayText("Loading grant...");

  HTTPClient https;
  https.setTimeout(10000);

  if (!https.begin(secureClient, GRANT_API_URL)) {
    Serial.println(F("Could not start HTTPS request."));
    updateDisplayText("API begin failed");
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("API HTTP status: "));
    Serial.println(httpCode);
    https.end();
    updateDisplayText("API request failed");
    return false;
  }

  String payload = https.getString();
  https.end();

  Serial.print(F("API response: "));
  Serial.println(payload);

  char grantMessage[sizeof(scrollText)];
  if (!buildGrantMessage(payload, grantMessage, sizeof(grantMessage))) {
    updateDisplayText("API parse failed");
    return false;
  }

  updateDisplayText(grantMessage);
  return true;
}

void scheduleNextGrantFetch(bool success) {
  nextGrantFetchAt = millis() + (success ? API_REFRESH_INTERVAL_MS : API_RETRY_INTERVAL_MS);
}

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println(F("ESP8266 MAX7219 grant display"));
  Serial.println(F("Fetching: https://nounv2api.vercel.app/api/grant"));
  Serial.println(F("Open Serial Monitor at 115200 baud to view logs or type manual display text."));

  matrix.begin();
  matrix.setIntensity(BRIGHTNESS);
  matrix.displayClear();
  matrix.displayScroll(scrollText, PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED_MS);

  secureClient.setInsecure();
  scheduleNextGrantFetch(fetchGrantData());
}

void loop() {
  readSerialText();
  serviceDisplay();

  if (timeReached(nextGrantFetchAt)) {
    scheduleNextGrantFetch(fetchGrantData());
  }
}
