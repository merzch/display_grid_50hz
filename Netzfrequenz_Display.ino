/*
  ESP32-1732S019
  - verbindet sich mit lokalem WLAN
  - lädt XML von https://dat.netzfrequenzmessung.de:9080/frequenz.xml
  - extrahiert die Netzfrequenz aus <f>...</f>
  - zeigt sie groß auf dem Display an
  - Abfrageintervall per #define einstellbar (Default 5 s)
*/

/*
  ESP32-1732S019
  Netzfrequenzanzeige mit:
  - WLAN
  - XML Abruf
  - große Anzeige
  - Farbwechsel nach Abweichung von 50 Hz
  - einstellbares Updateintervall
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Arduino_GFX_Library.h>

// =====================================================
// Konfiguration
// =====================================================
#define WIFI_SSID           "Kreatron2"
#define WIFI_PASSWORD       "gast2016"

#define FREQUENCY_URL          "https://dat.netzfrequenzmessung.de:9080/frequenz.xml"
#define UPDATE_INTERVAL_MS     5000UL
#define HTTP_TIMEOUT_MS        8000
#define SCREEN_ROTATION        1

#define TARGET_FREQUENCY       50.000f

// Farbschwellen
#define DEV_GREEN_MAX          0.020f
#define DEV_YELLOW_MAX         0.050f

// Display-Helligkeit
#define BACKLIGHT_BRIGHTNESS   255

// =====================================================
// Display-Konfiguration ESP32-1732S019
// =====================================================
#define TFT_BL    14
#define TFT_DC    11
#define TFT_CS    10
#define TFT_SCK   12
#define TFT_MOSI  13
#define TFT_RST   1

Arduino_DataBus *bus = new Arduino_ESP32SPI(
  TFT_DC, TFT_CS, TFT_SCK, TFT_MOSI, GFX_NOT_DEFINED
);

Arduino_GFX *gfx = new Arduino_ST7789(
  bus,
  TFT_RST,
  SCREEN_ROTATION,
  true,
  170, 320,
  35, 0,
  35, 0
);

// =====================================================
// Globale Variablen
// =====================================================
unsigned long lastUpdate = 0;
String lastFrequencyStr = "---.--";
String lastTimestamp = "";
String lastStatus = "Start...";
float lastFrequencyValue = TARGET_FREQUENCY;

// =====================================================
// Hilfsfunktionen
// =====================================================
uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

uint16_t getDeviationColor(float freq) {
  float dev = fabs(freq - TARGET_FREQUENCY);

  if (dev < DEV_GREEN_MAX) {
    return rgb565(0, 210, 80);     // grün
  } else if (dev < DEV_YELLOW_MAX) {
    return rgb565(255, 190, 0);    // gelb
  } else {
    return rgb565(220, 40, 40);    // rot
  }
}

String formatFrequency(float freq) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%.3f", freq);
  return String(buf);
}

String formatDeviation(float freq) {
  char buf[16];
  float dev = freq - TARGET_FREQUENCY;
  snprintf(buf, sizeof(buf), "%+.3f Hz", dev);
  return String(buf);
}

void setBacklight(uint8_t brightness = 255) {
  pinMode(TFT_BL, OUTPUT);
  analogWrite(TFT_BL, brightness);
}

void drawCenteredText(const String &text, int16_t y, uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setTextSize(size);
  gfx->getTextBounds(text, 0, y, &x1, &y1, &w, &h);
  int16_t x = (gfx->width() - w) / 2;
  gfx->setCursor(x, y);
  gfx->setTextColor(color);
  gfx->print(text);
}

String extractXmlTag(const String &xml, const String &tagName) {
  String startTag = "<" + tagName + ">";
  String endTag   = "</" + tagName + ">";

  int startPos = xml.indexOf(startTag);
  if (startPos < 0) return "";

  startPos += startTag.length();
  int endPos = xml.indexOf(endTag, startPos);
  if (endPos < 0) return "";

  String value = xml.substring(startPos, endPos);
  value.trim();
  return value;
}

bool fetchFrequency(float &frequency, String &timestamp, String &statusText) {
  if (WiFi.status() != WL_CONNECTED) {
    statusText = "WLAN getrennt";
    return false;
  }

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(client, FREQUENCY_URL)) {
    statusText = "HTTP begin fehlgeschlagen";
    return false;
  }

  int httpCode = http.GET();
  if (httpCode <= 0) {
    statusText = "HTTP Fehler";
    http.end();
    return false;
  }

  if (httpCode != HTTP_CODE_OK) {
    statusText = "HTTP Status " + String(httpCode);
    http.end();
    return false;
  }

  String payload = http.getString();
  http.end();

  String f = extractXmlTag(payload, "f");
  String z = extractXmlTag(payload, "z");

  if (f.isEmpty()) {
    statusText = "Tag <f> fehlt";
    return false;
  }

  frequency = f.toFloat();
  timestamp = z;
  statusText = "Aktualisiert";
  return true;
}

void drawHeader() {
  gfx->fillRect(0, 0, gfx->width(), 34, rgb565(20, 20, 28));
  drawCenteredText("Netzfrequenz", 10, 2, WHITE);
}

void drawFooter(const String &status, const String &timestamp) {
  int footerY = gfx->height() - 34;
  gfx->fillRect(0, footerY, gfx->width(), 34, rgb565(20, 20, 28));

  gfx->setTextSize(1);
  gfx->setTextColor(LIGHTGREY);
  gfx->setCursor(6, footerY + 6);
  gfx->print(status);

  gfx->setCursor(6, footerY + 18);
  if (timestamp.length() > 0) {
    gfx->print(timestamp);
  } else {
    gfx->print("keine Zeitinfo");
  }
}

void drawBar(float freq, uint16_t color) {
  const int barX = 32;
  //const int barY = 210;
  const int barY = 135;

  const int barW = gfx->width() - (2*barX);
  const int barH = 12;

  gfx->drawRect(barX, barY, barW, barH, WHITE);

  int centerX = barX + barW / 2;
  gfx->drawLine(centerX, barY - 4, centerX, barY + barH + 4, WHITE);

  float dev = freq - TARGET_FREQUENCY;

  // Bereich +-0.100 Hz auf Balkenbreite abbilden
  float normalized = dev / 0.100f;
  if (normalized > 1.0f) normalized = 1.0f;
  if (normalized < -1.0f) normalized = -1.0f;

  int fillX = centerX;
  int delta = (int)((barW / 2 - 2) * normalized);

  if (delta >= 0) {
    gfx->fillRect(fillX, barY + 2, delta, barH - 4, color);
  } else {
    gfx->fillRect(fillX + delta, barY + 2, -delta, barH - 4, color);
  }

  gfx->setTextSize(1);
  gfx->setTextColor(LIGHTGREY);
  gfx->setCursor(barX, barY + 16);
  gfx->print("-0.10");

  gfx->setCursor(centerX - 10, barY + 16);
  gfx->print("50.0");

  gfx->setCursor(barX + barW - 28, barY + 16);
  gfx->print("+0.10");
}

void drawFrequencyScreen(float freq, const String &status, const String &timestamp) {
  uint16_t mainColor = getDeviationColor(freq);
  float devAbs = fabs(freq - TARGET_FREQUENCY);

  gfx->fillScreen(BLACK);

  drawHeader();

  // farbige Fläche für Hauptwert
  int boxX = 10;
  int boxY = 52;
  int boxW = gfx->width() - 20;
  int boxH = 115;

  gfx->fillRoundRect(boxX, boxY, boxW, boxH, 14, rgb565(18, 18, 24));
  gfx->drawRoundRect(boxX, boxY, boxW, boxH, 14, mainColor);
  gfx->drawRoundRect(boxX + 1, boxY + 1, boxW - 2, boxH - 2, 14, mainColor);

  // Frequenz groß
  String freqText = formatFrequency(freq);
  drawCenteredText(freqText, 62, 4, mainColor);
  //drawCenteredText("Hz", 132, 2, WHITE);

  // Abweichung
  String devText = formatDeviation(freq);
  //drawCenteredText(devText, 178, 2, mainColor);
  drawCenteredText(devText, 95, 2, mainColor);

  // Qualitätsinfo
  String stateText;
  if (devAbs < DEV_GREEN_MAX) {
    stateText = "nahe Sollwert";
  } else if (devAbs < DEV_YELLOW_MAX) {
    stateText = "leichte Abweichung";
  } else {
    stateText = "deutliche Abweichung";
  }
  //drawCenteredText(stateText, 198, 1, LIGHTGREY);
  drawCenteredText(stateText, 115, 1, LIGHTGREY);

  drawBar(freq, mainColor);
  //mec drawFooter(status, timestamp);
}

void showStartupScreen(const String &line1, const String &line2 = "") {
  gfx->fillScreen(BLACK);
  drawHeader();
  drawCenteredText(line1, 100, 2, WHITE);
  if (line2.length() > 0) {
    drawCenteredText(line2, 130, 1, LIGHTGREY);
  }
}

void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  showStartupScreen("Verbinde WLAN...", WIFI_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    if (millis() - start > 20000) {
      showStartupScreen("WLAN Timeout", "Neuer Versuch...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      start = millis();
    }
  }

  showStartupScreen("WLAN verbunden", WiFi.localIP().toString());
  delay(1000);
}

// =====================================================
// Arduino
// =====================================================
void setup() {
  Serial.begin(115200);
  delay(300);

  gfx->begin();
  setBacklight(BACKLIGHT_BRIGHTNESS);

  showStartupScreen("ESP32-1732S019", "Netzfrequenzmonitor");
  delay(1000);

  connectWiFi();

  drawFrequencyScreen(lastFrequencyValue, "Erste Abfrage...", "");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }

  unsigned long now = millis();
  if (lastUpdate == 0 || (now - lastUpdate >= UPDATE_INTERVAL_MS)) {
    lastUpdate = now;

    float frequency;
    String timestamp;
    String statusText;

    if (fetchFrequency(frequency, timestamp, statusText)) {
      lastFrequencyValue = frequency;
      lastFrequencyStr = formatFrequency(frequency);
      lastTimestamp = timestamp;
      lastStatus = statusText;

      Serial.print("Frequenz: ");
      Serial.print(lastFrequencyStr);
      Serial.println(" Hz");
    } else {
      lastStatus = statusText;
      Serial.print("Fehler: ");
      Serial.println(statusText);
    }

    drawFrequencyScreen(lastFrequencyValue, lastStatus, lastTimestamp);
  }

  delay(50);
}