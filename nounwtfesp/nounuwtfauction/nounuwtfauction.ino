/*
  ESP8266 + MAX7219 noun.wtf auction display with WiFi setup portal

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
#include <EEPROM.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>
#include <MD_MAX72xx.h>
#include <MD_Parola.h>
#include <SPI.h>
#include <WiFiClientSecureBearSSL.h>
#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define HARDWARE_TYPE MD_MAX72XX::FC16_HW
#define MAX_DEVICES 4

const uint8_t CS_PIN = 15;  // D8 / GPIO15 on NodeMCU and Wemos D1 mini.

const uint8_t BRIGHTNESS = 3;
const uint16_t SCROLL_SPEED_MS = 40;
const uint16_t SCROLL_PAUSE_MS = 1000;

const char AP_SSID[] = "NOUNWTF-SETUP";
const char AUCTION_API_URL[] = "https://nounv2api.vercel.app/api/auction";
const char STARTUP_TEXT[] = "noun.wtf";

const unsigned long STARTUP_LETTER_INTERVAL_MS = 350;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000;
const unsigned long API_REFRESH_INTERVAL_MS = 60000; // 1 minute.
const unsigned long API_RETRY_INTERVAL_MS = 30000;   // 30 seconds after failure.
const unsigned long TIME_LEFT_DISPLAY_HOLD_MS = 15000;
const unsigned long TIME_LEFT_REDRAW_INTERVAL_MS = 250;

const int EEPROM_SIZE = 128;
const int SSID_ADDR = 0;
const int PASS_ADDR = 64;
const int MAX_SSID_LEN = 32;
const int MAX_PASS_LEN = 64;

const uint8_t MAX_AUCTION_MESSAGES = 3;
const uint8_t NO_TIME_LEFT_MESSAGE = 255;
const size_t MESSAGE_BUFFER_SIZE = 128;

MD_Parola matrix = MD_Parola(HARDWARE_TYPE, CS_PIN, MAX_DEVICES);
BearSSL::WiFiClientSecure secureClient;
ESP8266WebServer server(80);

String savedSsid;
String savedPassword;

char scrollText[MESSAGE_BUFFER_SIZE] = "NOUN.WTF";
char auctionMessages[MAX_AUCTION_MESSAGES][MESSAGE_BUFFER_SIZE];
uint8_t auctionMessageCount = 0;
uint8_t currentAuctionMessage = 0;
uint8_t timeLeftMessageIndex = NO_TIME_LEFT_MESSAGE;

bool setupMode = false;
bool serverStarted = false;
unsigned long nextAuctionFetchAt = 0;
unsigned long auctionTimeLeftSeconds = 0;
unsigned long auctionTimeLeftCapturedAt = 0;
unsigned long timeLeftDisplayStartedAt = 0;
unsigned long lastTimeLeftRedrawAt = 0;

bool timeReached(unsigned long targetTime) {
  return (long)(millis() - targetTime) >= 0;
}

void clearAuctionMessages() {
  auctionMessageCount = 0;
  currentAuctionMessage = 0;
  timeLeftMessageIndex = NO_TIME_LEFT_MESSAGE;
  timeLeftDisplayStartedAt = 0;
  lastTimeLeftRedrawAt = 0;
}

void updateDisplayText(const char *text) {
  strncpy(scrollText, text, sizeof(scrollText) - 1);
  scrollText[sizeof(scrollText) - 1] = '\0';

  matrix.displayClear();
  matrix.displayScroll(scrollText, PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED_MS);

  Serial.print(F("Display text: "));
  Serial.println(scrollText);
}

void updateStatusText(const char *text) {
  clearAuctionMessages();
  updateDisplayText(text);
}

void displayStaticText(const char *text) {
  matrix.displayClear();
  matrix.setTextAlignment(PA_CENTER);
  matrix.print(text);
}

bool parseTimeLeftSeconds(const char *timeLeft, unsigned long &seconds) {
  unsigned int first = 0;
  unsigned int second = 0;
  unsigned int third = 0;

  if (sscanf(timeLeft, "%u:%u:%u", &first, &second, &third) == 3) {
    seconds = (unsigned long)first * 3600UL + (unsigned long)second * 60UL + (unsigned long)third;
    return true;
  }

  if (sscanf(timeLeft, "%u:%u", &first, &second) == 2) {
    seconds = (unsigned long)first * 3600UL + (unsigned long)second * 60UL;
    return true;
  }

  return false;
}

unsigned long currentTimeLeftSeconds() {
  unsigned long elapsedSeconds = (millis() - auctionTimeLeftCapturedAt) / 1000UL;

  if (elapsedSeconds >= auctionTimeLeftSeconds) {
    return 0;
  }

  return auctionTimeLeftSeconds - elapsedSeconds;
}

void formatTimeLeft(char *output, size_t outputSize) {
  if (outputSize == 0) return;

  unsigned long remaining = currentTimeLeftSeconds();
  unsigned long hours = remaining / 3600UL;
  unsigned long minutes = (remaining % 3600UL) / 60UL;
  unsigned long seconds = remaining % 60UL;

  snprintf(output, outputSize, "%02lu:%02lu:%02lu", hours, minutes, seconds);
  output[outputSize - 1] = '\0';
}

uint8_t smallDigitColumn(char c, uint8_t column) {
  static const uint8_t digitColumns[10][3] = {
    {0x1f, 0x11, 0x1f}, // 0
    {0x00, 0x1f, 0x00}, // 1
    {0x1d, 0x15, 0x17}, // 2
    {0x15, 0x15, 0x1f}, // 3
    {0x07, 0x04, 0x1f}, // 4
    {0x17, 0x15, 0x1d}, // 5
    {0x1f, 0x15, 0x1d}, // 6
    {0x01, 0x1d, 0x03}, // 7
    {0x1f, 0x15, 0x1f}, // 8
    {0x17, 0x15, 0x1f}  // 9
  };

  if (c < '0' || c > '9' || column >= 3) {
    return 0;
  }

  return digitColumns[c - '0'][column];
}

uint8_t smallCharWidth(char c) {
  return c == ':' ? 1 : 3;
}

uint8_t smallTextWidth(const char *text) {
  uint8_t width = 0;

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    if (i > 0) width++;
    width += smallCharWidth(text[i]);
  }

  return width;
}

void drawSmallColumn(uint8_t x, uint8_t y, uint8_t bits) {
  const uint8_t displayWidth = MAX_DEVICES * 8;
  if (x >= displayWidth) return;

  uint16_t column = displayWidth - 1 - x;

  for (uint8_t row = 0; row < 5; row++) {
    if (bits & (1 << row)) {
      matrix.getGraphicObject()->setPoint(y + row, column, true);
    }
  }
}

void drawSmallTimeText(const char *text) {
  MD_MAX72XX *mx = matrix.getGraphicObject();
  const uint8_t displayWidth = MAX_DEVICES * 8;
  uint8_t textWidth = smallTextWidth(text);
  uint8_t x = textWidth < displayWidth ? (displayWidth - textWidth) / 2 : 0;
  const uint8_t y = 1;

  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::OFF);
  mx->clear();

  for (uint8_t i = 0; text[i] != '\0'; i++) {
    if (i > 0) x++;

    if (text[i] == ':') {
      drawSmallColumn(x, y, 0x0a);
      x++;
      continue;
    }

    for (uint8_t col = 0; col < 3; col++) {
      drawSmallColumn(x + col, y, smallDigitColumn(text[i], col));
    }

    x += 3;
  }

  mx->update();
  mx->control(MD_MAX72XX::UPDATE, MD_MAX72XX::ON);
}

void updateTimeLeftMessage() {
  if (timeLeftMessageIndex == NO_TIME_LEFT_MESSAGE || timeLeftMessageIndex >= auctionMessageCount) {
    return;
  }

  formatTimeLeft(auctionMessages[timeLeftMessageIndex], sizeof(auctionMessages[timeLeftMessageIndex]));
}

void drawTimeLeftDisplay() {
  if (timeLeftMessageIndex == NO_TIME_LEFT_MESSAGE || timeLeftMessageIndex >= auctionMessageCount) {
    return;
  }

  updateTimeLeftMessage();
  drawSmallTimeText(auctionMessages[timeLeftMessageIndex]);
  lastTimeLeftRedrawAt = millis();
}

void showAuctionMessage(uint8_t messageIndex) {
  currentAuctionMessage = messageIndex;

  if (currentAuctionMessage == timeLeftMessageIndex) {
    timeLeftDisplayStartedAt = millis();
    drawTimeLeftDisplay();
    Serial.print(F("Display time left: "));
    Serial.println(auctionMessages[currentAuctionMessage]);
    return;
  }

  updateDisplayText(auctionMessages[currentAuctionMessage]);
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
  if (auctionMessageCount > 1 && currentAuctionMessage == timeLeftMessageIndex) {
    if (millis() - timeLeftDisplayStartedAt >= TIME_LEFT_DISPLAY_HOLD_MS) {
      currentAuctionMessage = (currentAuctionMessage + 1) % auctionMessageCount;
      showAuctionMessage(currentAuctionMessage);
      return;
    }

    if (millis() - lastTimeLeftRedrawAt >= TIME_LEFT_REDRAW_INTERVAL_MS) {
      drawTimeLeftDisplay();
    }

    return;
  }

  if (matrix.displayAnimate()) {
    delay(SCROLL_PAUSE_MS);

    if (auctionMessageCount > 1) {
      currentAuctionMessage = (currentAuctionMessage + 1) % auctionMessageCount;
      showAuctionMessage(currentAuctionMessage);
    } else {
      matrix.displayReset();
    }
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
        updateStatusText(inputBuffer);
      }

      inputIndex = 0;
      return;
    }

    if (inputIndex < sizeof(inputBuffer) - 1) {
      inputBuffer[inputIndex++] = c;
    }
  }
}

void saveCredentials(const String &ssid, const String &password) {
  auto writeField = [](int address, int maxLen, const String &value) {
    int len = min((int)value.length(), maxLen - 1);
    int i = 0;

    for (; i < len; i++) {
      EEPROM.write(address + i, value[i]);
    }

    for (; i < maxLen; i++) {
      EEPROM.write(address + i, 0);
    }
  };

  writeField(SSID_ADDR, MAX_SSID_LEN, ssid);
  writeField(PASS_ADDR, MAX_PASS_LEN, password);
  EEPROM.commit();
}

String readStringFromEEPROM(int address, int maxLen) {
  String value;
  value.reserve(maxLen);

  for (int i = 0; i < maxLen; i++) {
    int raw = EEPROM.read(address + i);
    if (raw == 0 || raw == 255) break;
    value += char(raw);
  }

  return value;
}

void sendEscaped(const String &value) {
  for (unsigned int i = 0; i < value.length(); i++) {
    char c = value[i];

    switch (c) {
      case '&':
        server.sendContent(F("&amp;"));
        break;
      case '"':
        server.sendContent(F("&quot;"));
        break;
      case '<':
        server.sendContent(F("&lt;"));
        break;
      case '>':
        server.sendContent(F("&gt;"));
        break;
      default:
        server.sendContent(String(c));
        break;
    }
  }
}

void sendHtmlPage(const String &message) {
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, "text/html", "");

  server.sendContent(F(
    "<!doctype html><html><head>"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>NOUN.WTF Auction Setup</title>"
    "<style>body{font-family:Arial,sans-serif;margin:24px;background:#f6f7f8;color:#1f2933}"
    "main{max-width:430px;margin:auto;background:#fff;border:1px solid #d7dde3;border-radius:8px;padding:20px}"
    "h1{font-size:24px;margin:0 0 14px}p{line-height:1.45}label{display:block;margin-top:14px;font-weight:600}"
    "select,input,button{width:100%;box-sizing:border-box;margin-top:6px;padding:10px;font-size:16px}"
    "button{margin-top:18px;background:#111827;color:#fff;border:0;border-radius:6px}.msg{margin:0 0 12px;color:#b45309}"
    ".small{font-size:13px;color:#4b5563}</style>"
    "</head><body><main><h1>NOUN.WTF Auction WiFi Setup</h1>"));

  if (message.length()) {
    server.sendContent(F("<p class=\"msg\">"));
    server.sendContent(message);
    server.sendContent(F("</p>"));
  }

  if (setupMode) {
    server.sendContent(F("<p class=\"small\">Connected to setup AP <strong>NOUNWTF-SETUP</strong>. Save 2.4GHz WiFi credentials below.</p>"));
  } else {
    server.sendContent(F("<p class=\"small\">Device is online. Current IP: "));
    server.sendContent(WiFi.localIP().toString());
    server.sendContent(F("</p>"));
  }

  server.sendContent(F(
    "<form method=\"post\" action=\"/save\">"
    "<label for=\"ssid\">Network</label><select id=\"ssid\" name=\"ssid\">"));

  int networkCount = WiFi.scanNetworks();

  if (networkCount == 0) {
    server.sendContent(F("<option value=\"\">No networks found</option>"));
  } else {
    for (int i = 0; i < networkCount; i++) {
      server.sendContent(F("<option value=\""));
      sendEscaped(WiFi.SSID(i));
      server.sendContent(F("\">"));
      sendEscaped(WiFi.SSID(i));
      server.sendContent(F(" ("));
      server.sendContent(String(WiFi.RSSI(i)));
      server.sendContent(F(" dBm)</option>"));
    }
  }

  server.sendContent(F(
    "</select>"
    "<label for=\"manual\">Or type SSID</label><input id=\"manual\" name=\"manual\" maxlength=\"32\" autocomplete=\"off\">"
    "<label for=\"password\">Password</label><input id=\"password\" name=\"password\" type=\"password\" maxlength=\"64\">"
    "<button type=\"submit\">Save and Restart</button></form>"
    "<form method=\"post\" action=\"/clear\"><button type=\"submit\">Clear Saved WiFi</button></form>"
    "</main></body></html>"));

  server.sendContent("");
}

void handleRoot() {
  sendHtmlPage(setupMode ? F("Enter WiFi credentials to connect the auction display.") : F("Auction display is connected to WiFi."));
}

void handleSave() {
  String ssid = server.arg("manual");
  if (ssid.length() == 0) ssid = server.arg("ssid");

  String password = server.arg("password");
  ssid.trim();

  if (ssid.length() == 0) {
    sendHtmlPage(F("SSID is required."));
    return;
  }

  saveCredentials(ssid, password);

  server.send(200, "text/html",
              F("<!doctype html><html><head><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
                "<meta http-equiv=\"refresh\" content=\"8;url=/\"></head><body>"
                "<p>Saved. Restarting and connecting to WiFi...</p></body></html>"));

  delay(1000);
  ESP.restart();
}

void handleClear() {
  saveCredentials("", "");
  server.send(200, "text/html", F("<p>Saved WiFi cleared. Restarting...</p>"));
  delay(1000);
  ESP.restart();
}

void startWebServer() {
  if (serverStarted) return;

  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/clear", HTTP_POST, handleClear);
  server.begin();

  serverStarted = true;
  Serial.println(F("Web server started on port 80."));
}

void startSetupMode() {
  if (setupMode) return;

  setupMode = true;
  clearAuctionMessages();

  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID);

  Serial.println(F("Setup mode started."));
  Serial.print(F("Connect to AP: "));
  Serial.println(AP_SSID);
  Serial.print(F("Open: http://"));
  Serial.println(WiFi.softAPIP());

  updateStatusText("setup wifi 192.168.4.1");
}

bool connectSavedWiFi() {
  if (savedSsid.length() == 0) {
    Serial.println(F("No saved WiFi credentials."));
    return false;
  }

  serviceStartupText();
  WiFi.mode(WIFI_STA);
  WiFi.begin(savedSsid.c_str(), savedPassword.c_str());

  Serial.print(F("Connecting to saved WiFi: "));
  Serial.println(savedSsid);

  unsigned long startedAt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startedAt < WIFI_CONNECT_TIMEOUT_MS) {
    serviceStartupText();
    delay(50);
    Serial.print('.');
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    setupMode = false;

    Serial.print(F("WiFi connected. IP: "));
    Serial.println(WiFi.localIP());

    updateStatusText("WiFi connected");
    return true;
  }

  Serial.println(F("WiFi connection failed."));
  WiFi.disconnect();
  return false;
}

bool readJsonStringValue(const String &json, const char *key, int startAt, int endAt, char *output, size_t outputSize) {
  if (outputSize == 0) return false;
  if (startAt < 0 || endAt <= startAt) return false;

  String quotedKey = String('"') + key + '"';
  int keyPosition = json.indexOf(quotedKey, startAt);
  if (keyPosition < 0 || keyPosition >= endAt) return false;

  int colonPosition = json.indexOf(':', keyPosition + quotedKey.length());
  if (colonPosition < 0 || colonPosition >= endAt) return false;

  int valueStart = colonPosition + 1;
  while (valueStart < endAt && isspace((unsigned char)json[valueStart])) {
    valueStart++;
  }

  if (valueStart >= endAt || json[valueStart] != '"') return false;

  size_t outputIndex = 0;
  bool escaped = false;

  for (int i = valueStart + 1; i < endAt && i < (int)json.length(); i++) {
    char c = json[i];

    if (escaped) {
      switch (c) {
        case '"':
        case '\\':
        case '/':
          break;
        case 'n':
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

bool readJsonBoolValue(const String &json, const char *key, int startAt, int endAt, bool &value) {
  if (startAt < 0 || endAt <= startAt) return false;

  String quotedKey = String('"') + key + '"';
  int keyPosition = json.indexOf(quotedKey, startAt);
  if (keyPosition < 0 || keyPosition >= endAt) return false;

  int colonPosition = json.indexOf(':', keyPosition + quotedKey.length());
  if (colonPosition < 0 || colonPosition >= endAt) return false;

  int valueStart = colonPosition + 1;
  while (valueStart < endAt && isspace((unsigned char)json[valueStart])) {
    valueStart++;
  }

  if (json.indexOf("true", valueStart) == valueStart) {
    value = true;
    return true;
  }

  if (json.indexOf("false", valueStart) == valueStart) {
    value = false;
    return true;
  }

  return false;
}

void appendChar(char *output, size_t outputSize, size_t &outputIndex, char c) {
  if (outputIndex < outputSize - 1) {
    output[outputIndex++] = c;
  }
}

void sanitizeForDisplay(const char *input, char *output, size_t outputSize) {
  if (outputSize == 0) return;

  size_t inputLen = strlen(input);
  size_t outputIndex = 0;

  for (size_t i = 0; i < inputLen; i++) {
    uint8_t c = (uint8_t)input[i];

    if (c < 128) {
      appendChar(output, outputSize, outputIndex, (char)c);
      continue;
    }

    uint8_t next1 = (i + 1 < inputLen) ? (uint8_t)input[i + 1] : 0;
    uint8_t next2 = (i + 2 < inputLen) ? (uint8_t)input[i + 2] : 0;

    if (c == 0xE2 && next1 == 0x80 && (next2 == 0x93 || next2 == 0x94)) {
      appendChar(output, outputSize, outputIndex, '-');
      i += 2;
      continue;
    }

    if (c == 0xE2 && next1 == 0x80 && next2 == 0xA6) {
      appendChar(output, outputSize, outputIndex, '.');
      appendChar(output, outputSize, outputIndex, '.');
      appendChar(output, outputSize, outputIndex, '.');
      i += 2;
      continue;
    }

    if (c == 0xE2 && next1 == 0x80 && (next2 == 0x98 || next2 == 0x99)) {
      appendChar(output, outputSize, outputIndex, '\'');
      i += 2;
      continue;
    }

    if (c == 0xE2 && next1 == 0x80 && (next2 == 0x9C || next2 == 0x9D)) {
      appendChar(output, outputSize, outputIndex, '"');
      i += 2;
      continue;
    }

    if ((c & 0xE0) == 0xC0) {
      i += 1;
    } else if ((c & 0xF0) == 0xE0) {
      i += 2;
    } else if ((c & 0xF8) == 0xF0) {
      i += 3;
    }
  }

  output[outputIndex] = '\0';
}

int findJsonObjectEnd(const String &json, int objectStart) {
  int depth = 0;
  bool inString = false;
  bool escaped = false;

  for (int i = objectStart; i < (int)json.length(); i++) {
    char c = json[i];

    if (escaped) {
      escaped = false;
      continue;
    }

    if (inString) {
      if (c == '\\') {
        escaped = true;
      } else if (c == '"') {
        inString = false;
      }
      continue;
    }

    if (c == '"') {
      inString = true;
    } else if (c == '{') {
      depth++;
    } else if (c == '}') {
      depth--;
      if (depth == 0) {
        return i + 1;
      }
    }
  }

  return -1;
}

void addAuctionMessage(const char *rawMessage) {
  if (auctionMessageCount >= MAX_AUCTION_MESSAGES) return;

  sanitizeForDisplay(rawMessage, auctionMessages[auctionMessageCount], sizeof(auctionMessages[auctionMessageCount]));

  if (auctionMessages[auctionMessageCount][0] != '\0') {
    Serial.print(F("Auction message "));
    Serial.print(auctionMessageCount + 1);
    Serial.print(F(": "));
    Serial.println(auctionMessages[auctionMessageCount]);
    auctionMessageCount++;
  }
}

bool buildAuctionMessages(const String &payload) {
  int dataPosition = payload.indexOf("\"data\"");
  if (dataPosition < 0) return false;

  int objectStart = payload.indexOf('{', dataPosition);
  if (objectStart < 0) return false;

  int objectEnd = findJsonObjectEnd(payload, objectStart);
  if (objectEnd < 0) return false;

  char nounId[16];
  char amountETH[32] = "0";
  char timeLeft[24] = "--:--";
  char bidder[64] = "no bidder";
  char bidderAddress[48];
  char rawMessage[160];
  bool settled = false;

  if (!readJsonStringValue(payload, "nounId", objectStart, objectEnd, nounId, sizeof(nounId))) return false;
  readJsonStringValue(payload, "amountETH", objectStart, objectEnd, amountETH, sizeof(amountETH));
  readJsonStringValue(payload, "timeLeft", objectStart, objectEnd, timeLeft, sizeof(timeLeft));
  auctionTimeLeftSeconds = 0;
  parseTimeLeftSeconds(timeLeft, auctionTimeLeftSeconds);
  auctionTimeLeftCapturedAt = millis();

  if (!readJsonStringValue(payload, "bidder", objectStart, objectEnd, bidder, sizeof(bidder))) {
    if (readJsonStringValue(payload, "bidderAddress", objectStart, objectEnd, bidderAddress, sizeof(bidderAddress))) {
      strncpy(bidder, bidderAddress, sizeof(bidder) - 1);
      bidder[sizeof(bidder) - 1] = '\0';
    }
  }

  readJsonBoolValue(payload, "settled", objectStart, objectEnd, settled);

  clearAuctionMessages();

  if (settled) {
    snprintf(rawMessage, sizeof(rawMessage), "NOUN #%s | SETTLED", nounId);
  } else if (strcmp(amountETH, "0") == 0 || strcmp(amountETH, "0.0") == 0 || strcmp(bidder, "no bidder") == 0) {
    snprintf(rawMessage, sizeof(rawMessage), "NOUN #%s | NO BID", nounId);
  } else {
    snprintf(rawMessage, sizeof(rawMessage), "NOUN #%s | %s ETH", nounId, amountETH);
  }
  rawMessage[sizeof(rawMessage) - 1] = '\0';
  addAuctionMessage(rawMessage);

  snprintf(rawMessage, sizeof(rawMessage), "%s %s", settled ? "WINNER" : "BIDDER", bidder);
  rawMessage[sizeof(rawMessage) - 1] = '\0';
  addAuctionMessage(rawMessage);

  timeLeftMessageIndex = auctionMessageCount;
  formatTimeLeft(rawMessage, sizeof(rawMessage));
  rawMessage[sizeof(rawMessage) - 1] = '\0';
  addAuctionMessage(rawMessage);

  return auctionMessageCount > 0;
}

bool fetchAuctionData() {
  if (WiFi.status() != WL_CONNECTED) {
    return false;
  }

  updateStatusText("Loading auction...");

  HTTPClient https;
  https.setTimeout(10000);

  if (!https.begin(secureClient, AUCTION_API_URL)) {
    Serial.println(F("Could not start HTTPS request."));
    updateStatusText("API begin failed");
    return false;
  }

  int httpCode = https.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.print(F("API HTTP status: "));
    Serial.println(httpCode);
    https.end();
    updateStatusText("API request failed");
    return false;
  }

  String payload = https.getString();
  https.end();

  Serial.print(F("API response: "));
  Serial.println(payload);

  if (!buildAuctionMessages(payload)) {
    updateStatusText("API parse failed");
    return false;
  }

  showAuctionMessage(0);
  return true;
}

void scheduleNextAuctionFetch(bool success) {
  nextAuctionFetchAt = millis() + (success ? API_REFRESH_INTERVAL_MS : API_RETRY_INTERVAL_MS);
}

void setup() {
  Serial.begin(115200);
  delay(250);

  Serial.println();
  Serial.println(F("NOUN.WTF ESP8266 auction firmware"));
  Serial.println(F("Open setup AP if saved WiFi is missing or fails."));
  Serial.println(F("Fetching: https://nounv2api.vercel.app/api/auction"));

  matrix.begin();
  matrix.setIntensity(BRIGHTNESS);
  matrix.displayClear();
  matrix.displayScroll(scrollText, PA_LEFT, PA_SCROLL_LEFT, SCROLL_SPEED_MS);

  EEPROM.begin(EEPROM_SIZE);
  savedSsid = readStringFromEEPROM(SSID_ADDR, MAX_SSID_LEN);
  savedPassword = readStringFromEEPROM(PASS_ADDR, MAX_PASS_LEN);

  secureClient.setInsecure();

  if (connectSavedWiFi()) {
    startWebServer();
    scheduleNextAuctionFetch(fetchAuctionData());
  } else {
    startSetupMode();
    startWebServer();
  }
}

void loop() {
  server.handleClient();
  readSerialText();
  serviceDisplay();

  if (setupMode) {
    return;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println(F("WiFi lost. Starting setup portal."));
    startSetupMode();
    startWebServer();
    return;
  }

  if (timeReached(nextAuctionFetchAt)) {
    scheduleNextAuctionFetch(fetchAuctionData());
  }
}
