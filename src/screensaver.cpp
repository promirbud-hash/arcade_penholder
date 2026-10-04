#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <DHT.h>

#include <Fonts/FreeMonoBold18pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeSansOblique18pt7b.h>
#include <Fonts/FreeSerif18pt7b.h>
#include <Fonts/FreeSerifBold18pt7b.h>

#include "arcade_runtime.h"
#include "pins.h"

namespace screensaver {
namespace {

constexpr uint32_t kSampleIntervalMs = 2500;
constexpr uint8_t kFontCount = 5;
constexpr uint16_t kTemperatureColor = ST77XX_RED;
constexpr uint16_t kHumidityColor = ST77XX_BLUE;
constexpr uint16_t kBlack = ST77XX_BLACK;
constexpr uint16_t kWhite = ST77XX_WHITE;

DHT sensor(pins::dht_data, DHT22);

const GFXfont* const fonts[kFontCount] = {
    nullptr,
    &FreeSansBold18pt7b,
    &FreeSerifBold18pt7b,
    &FreeMonoBold18pt7b,
    &FreeSansOblique18pt7b,
};
const char* const fontNames[kFontCount] = {
    "CLASSIC", "SANS BOLD", "SERIF", "MONO", "ITALIC",
};

uint8_t selectedFont = 0;
uint32_t lastSampleMs = 0;
float temperatureC = 0.0f;
float humidityPercent = 0.0f;
bool sampleValid = false;
bool sampleAttempted = false;
bool blackBackground = false;
bool previousLeft = false;
bool previousRight = false;
bool previousUp = false;
bool previousDown = false;
bool previousJoyButton = false;
bool previousP1 = false;
bool previousP2 = false;
bool previousP3 = false;
bool previousP4 = false;

Adafruit_ST7789& display() {
  return arcadeSharedDisplay();
}

bool pressed(int pin) {
  return digitalRead(pin) == LOW;
}

bool pressedEdge(int pin, bool& wasPressed) {
  const bool isPressed = pressed(pin);
  const bool edge = isPressed && !wasPressed;
  wasPressed = isPressed;
  return edge;
}

void syncButtons() {
  previousLeft = pressed(pins::joy_left);
  previousRight = pressed(pins::joy_right);
  previousUp = pressed(pins::joy_up);
  previousDown = pressed(pins::joy_down);
  previousJoyButton = pressed(pins::joy_button);
  previousP1 = pressed(pins::button_p1);
  previousP2 = pressed(pins::button_p2);
  previousP3 = pressed(pins::button_p3);
  previousP4 = pressed(pins::button_p4);
}

void drawCentered(const char* text, int16_t centerX, int16_t topY,
                  const GFXfont* font, uint8_t textSize, uint16_t color) {
  display().setFont(font);
  display().setTextSize(textSize);
  display().setTextColor(color);
  int16_t x1, y1;
  uint16_t width, height;
  display().getTextBounds(text, 0, 0, &x1, &y1, &width, &height);
  const int16_t cursorX = centerX - (x1 + static_cast<int16_t>(width / 2));
  const int16_t cursorY = topY - y1;
  display().setCursor(cursorX, cursorY);
  display().print(text);
}

void drawReadout(const char* label, const char* value, int16_t topY,
                 uint16_t valueColor) {
  display().setFont(nullptr);
  display().setTextSize(2);
  display().setTextColor(valueColor);
  int16_t labelX, labelY;
  uint16_t labelWidth, labelHeight;
  display().getTextBounds(label, 0, 0, &labelX, &labelY, &labelWidth, &labelHeight);
  display().setCursor((240 - labelWidth) / 2, topY);
  display().print(label);

  if (sampleValid) {
    drawCentered(value, 120, topY + 32, fonts[selectedFont],
                 selectedFont == 0 ? 4 : 1, valueColor);
  } else {
    drawCentered(value, 120, topY + 36, nullptr, 2, valueColor);
  }
}

void drawScreen() {
  display().fillScreen(blackBackground ? kBlack : kWhite);

  char temperatureText[20];
  char humidityText[20];
  if (sampleValid) {
    snprintf(temperatureText, sizeof(temperatureText), "%.1f C",
             temperatureC);
    snprintf(humidityText, sizeof(humidityText), "%.1f %%", humidityPercent);
  } else if (sampleAttempted) {
    snprintf(temperatureText, sizeof(temperatureText), "%s",
             arcadeText("BLAD ODCZYTU", "READ ERROR"));
    snprintf(humidityText, sizeof(humidityText), "%s",
             arcadeText("BLAD ODCZYTU", "READ ERROR"));
  } else {
    snprintf(temperatureText, sizeof(temperatureText), "%s",
             arcadeText("OCZEKIWANIE", "WAITING"));
    snprintf(humidityText, sizeof(humidityText), "%s",
             arcadeText("OCZEKIWANIE", "WAITING"));
  }

  drawReadout(arcadeText("TEMPERATURA", "TEMPERATURE"), temperatureText, 45,
              kTemperatureColor);
  drawReadout(arcadeText("WILGOTNOSC", "HUMIDITY"), humidityText, 178,
              kHumidityColor);

  display().setFont(nullptr);
  display().setTextSize(1);
  display().setTextColor(blackBackground ? kWhite : kBlack);
  display().setCursor(8, 285);
  display().print(fontNames[selectedFont]);
  display().setCursor(8, 302);
  display().print(arcadeText("LEWO/PRAWO: CZCIONKA  X: TLO",
                             "LEFT/RIGHT: FONT  X: BACKGROUND"));
}

void sampleSensor(uint32_t now) {
  if (now - lastSampleMs < kSampleIntervalMs) return;
  lastSampleMs = now;

  const float nextHumidity = sensor.readHumidity();
  const float nextTemperature = sensor.readTemperature();
  sampleAttempted = true;
  if (isnan(nextHumidity) || isnan(nextTemperature)) {
    sampleValid = false;
    Serial.println("DHT22 read failed on GPIO9.");
  } else {
    humidityPercent = nextHumidity;
    temperatureC = nextTemperature;
    sampleValid = true;
  }
  drawScreen();
}

}  // namespace

void begin() {
  selectedFont = 0;
  blackBackground = false;
  sampleValid = false;
  sampleAttempted = false;
  lastSampleMs = millis();

  SPI.begin(pins::tft_clk, pins::tft_miso, pins::tft_mosi, pins::tft_cs);
  display().init(240, 320);
  display().setRotation(0);
  display().invertDisplay(false);

  pinMode(pins::joy_left, INPUT_PULLUP);
  pinMode(pins::joy_right, INPUT_PULLUP);
  pinMode(pins::joy_up, INPUT_PULLUP);
  pinMode(pins::joy_down, INPUT_PULLUP);
  pinMode(pins::joy_button, INPUT_PULLUP);
  pinMode(pins::button_p1, INPUT_PULLUP);
  pinMode(pins::button_p2, INPUT_PULLUP);
  pinMode(pins::button_p3, INPUT_PULLUP);
  pinMode(pins::button_p4, INPUT_PULLUP);

  sensor.begin();
  syncButtons();
  drawScreen();
}

void tick() {
  const uint32_t now = millis();
  const bool leftEdge = pressedEdge(pins::joy_left, previousLeft);
  const bool rightEdge = pressedEdge(pins::joy_right, previousRight);
  if (leftEdge || rightEdge) {
    selectedFont = static_cast<uint8_t>(
        (selectedFont + kFontCount + (leftEdge ? kFontCount - 1 : 1)) %
        kFontCount);
    drawScreen();
  } else if (pressedEdge(pins::button_p3, previousP3)) {
    blackBackground = !blackBackground;
    drawScreen();
  } else if (pressedEdge(pins::joy_up, previousUp) ||
             pressedEdge(pins::joy_down, previousDown) ||
             pressedEdge(pins::joy_button, previousJoyButton) ||
             pressedEdge(pins::button_p1, previousP1) ||
             pressedEdge(pins::button_p2, previousP2) ||
             pressedEdge(pins::button_p4, previousP4)) {
    arcadeRequestGameExit();
    return;
  }

  sampleSensor(now);
}

}  // namespace screensaver
