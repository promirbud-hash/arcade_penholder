// Yellow Racer adaptation for Arcade.
// Based on Yellow Racer by Angelo Moroni, Copyright (c) 2026.
// Distributed under the MIT License; see LICENSE-Yellow-Racer.txt.
#include "yellowracer.h"

#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "arcade_runtime.h"
#include "arcade_persistence.h"
#include "pins.h"

namespace yellowracer {
namespace {

constexpr int16_t SCREEN_W = 240;
constexpr int16_t SCREEN_H = 320;
constexpr uint16_t TRACK_SEGMENTS = 800;
constexpr uint16_t DRAW_DISTANCE = 150;
constexpr float SEGMENT_LENGTH = 200.0f;
constexpr float ROAD_WIDTH = 2000.0f;
constexpr float CAMERA_HEIGHT = 1000.0f;
constexpr float CAMERA_DEPTH = 0.8390996f;
constexpr float MAX_SPEED = 7000.0f;
constexpr float STEER_SPEED = 2.4f;
constexpr float PLAYER_X_MAX = 1.4f;
constexpr float OFFROAD_MIN_SPEED = 800.0f;
constexpr float OFFROAD_DRAG = 2500.0f;
constexpr float CENTRIFUGAL = 0.4f;
constexpr uint8_t TOTAL_LAPS = 3;
constexpr float TRAFFIC_COLLISION_RANGE = 220.0f;
constexpr float TRAFFIC_COLLISION_LANE = 0.38f;

constexpr uint16_t SKY = 0x6BD7;
constexpr uint16_t GRASS_DARK = 0x0360;
constexpr uint16_t GRASS_LIGHT = 0x07C0;
constexpr uint16_t ROAD_DARK = 0x4208;
constexpr uint16_t ROAD_LIGHT = 0x5288;
constexpr uint16_t RUMBLE_DARK = 0x8400;
constexpr uint16_t RUMBLE_LIGHT = 0xFFFF;
constexpr uint16_t LANE = 0xFFFF;
constexpr uint16_t MOUNTAIN = 0x6B4D;
constexpr uint16_t SNOW = 0xFFFF;
constexpr uint16_t MENU_BUTTON = 0x4A9F;
constexpr uint16_t CAR_BODY = 0xD800;
constexpr uint16_t CAR_WING = 0xA800;
constexpr uint16_t CAR_GLASS = 0x2104;
constexpr uint16_t CAR_TIRE = 0x0000;
constexpr uint16_t CAR_LIGHT = 0xFFE0;

struct TrackDef {
  const char* name;
  int curveK1;
  float curveA1;
  int curveK2;
  float curveA2;
  int hillK1;
  float hillA1;
  int hillK2;
  float hillA2;
  uint16_t timeBudget;
};

struct Segment {
  float curve;
  float y;
};

struct Projected {
  float x;
  float y;
  float halfWidth;
};

struct TrafficCar {
  float distanceAhead;
  float speed;
  float lane;
  uint16_t color;
};

enum class State : uint8_t {
  Menu,
  Race,
  GameOver
};

constexpr TrackDef TRACKS[] = {
    {"EASY", 3, 1.6f, 7, 0.5f, 2, 700.0f, 5, 180.0f, 85},
    {"MEDIUM", 4, 2.4f, 9, 0.9f, 3, 1100.0f, 6, 280.0f, 80},
    {"HARD", 5, 3.2f, 11, 1.3f, 2, 1400.0f, 7, 380.0f, 75},
};

Adafruit_ST7789 panel(&SPI, pins::tft_cs, pins::tft_dc, pins::tft_rst);
uint16_t* frameBuffer = nullptr;

class FrameCanvas : public Adafruit_GFX {
 public:
  FrameCanvas() : Adafruit_GFX(SCREEN_W, SCREEN_H) {}

  void setBuffer(uint16_t* buffer) { buffer_ = buffer; }

  void writeFillRect(int16_t x, int16_t y, int16_t width, int16_t height,
                     uint16_t color) override {
    int32_t left = x;
    int32_t top = y;
    int32_t right = left + width;
    int32_t bottom = top + height;
    if (width <= 0 || height <= 0 || right <= 0 || bottom <= 0 ||
        left >= SCREEN_W || top >= SCREEN_H || buffer_ == nullptr) return;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > SCREEN_W) right = SCREEN_W;
    if (bottom > SCREEN_H) bottom = SCREEN_H;

    for (int32_t row = top; row < bottom; ++row) {
      uint16_t* pixel = buffer_ + static_cast<size_t>(row) * SCREEN_W + left;
      for (int32_t column = left; column < right; ++column) *pixel++ = color;
    }
  }

  void writeFastHLine(int16_t x, int16_t y, int16_t width,
                      uint16_t color) override {
    writeFillRect(x, y, width, 1, color);
  }

  void writeFastVLine(int16_t x, int16_t y, int16_t height,
                      uint16_t color) override {
    writeFillRect(x, y, 1, height, color);
  }

  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if (buffer_ && x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
      buffer_[static_cast<size_t>(y) * SCREEN_W + x] = color;
    }
  }

 private:
  uint16_t* buffer_ = nullptr;
};

FrameCanvas tft;
Segment* track = nullptr;
Projected* nearPoints = nullptr;
Projected* farPoints = nullptr;
TrafficCar* traffic = nullptr;
uint8_t trafficCount = 0;

State state = State::Menu;
uint8_t selectedTrack = 0;
uint8_t lap = 0;
float position = 0.0f;
float speed = 0.0f;
float playerX = 0.0f;
int32_t timeLeftMs = 0;
int32_t score = 0;
uint32_t highScores[3] = {};
bool won = false;
bool crashed = false;
uint32_t lapFlashMs = 0;
uint32_t lastTickMs = 0;
uint32_t lastFrameMs = 0;
uint32_t lastMenuInputMs = 0;
bool previousInvertButton = false;
bool previousExitButton = false;
bool previousJoyButton = false;
bool previousRestartButton = false;

void drawHudBox(int16_t x, int16_t y, int16_t width, int16_t height,
                const char* text);

uint16_t trackCount() {
  return TRACK_SEGMENTS;
}

float trackLength() {
  return trackCount() * SEGMENT_LENGTH;
}

void buildTrack(uint8_t trackId) {
  if (track == nullptr) return;
  const TrackDef& def = TRACKS[trackId % (sizeof(TRACKS) / sizeof(TRACKS[0]))];
  const float w1 = 2.0f * PI * def.curveK1 / TRACK_SEGMENTS;
  const float w2 = 2.0f * PI * def.curveK2 / TRACK_SEGMENTS;
  const float w3 = 2.0f * PI * def.hillK1 / TRACK_SEGMENTS;
  const float w4 = 2.0f * PI * def.hillK2 / TRACK_SEGMENTS;
  for (uint16_t i = 0; i < TRACK_SEGMENTS; ++i) {
    track[i].curve = sinf(w1 * i) * def.curveA1 +
                     sinf(w2 * i) * def.curveA2;
    track[i].y = sinf(w3 * i) * def.hillA1 +
                 sinf(w4 * i) * def.hillA2;
  }
}

uint16_t segmentAt(float worldPosition) {
  int32_t index = static_cast<int32_t>(worldPosition / SEGMENT_LENGTH);
  index %= TRACK_SEGMENTS;
  if (index < 0) index += TRACK_SEGMENTS;
  return static_cast<uint16_t>(index);
}

float curveAt(float worldPosition) {
  return track[segmentAt(worldPosition)].curve;
}

void project(Projected& point, float worldX, float worldY,
             float cameraX, float cameraY, float relativeZ) {
  if (relativeZ < 1.0f) relativeZ = 1.0f;
  const float scale = CAMERA_DEPTH / relativeZ;
  point.x = (SCREEN_W * 0.5f) +
            scale * (worldX - cameraX) * (SCREEN_W * 0.5f);
  point.y = (SCREEN_H * 0.5f) -
            scale * (worldY - cameraY) * (SCREEN_H * 0.5f);
  point.halfWidth = scale * ROAD_WIDTH * (SCREEN_W * 0.5f);
}

void fillQuad(int16_t x1, int16_t y1, int16_t x2, int16_t y2,
              int16_t x3, int16_t y3, int16_t x4, int16_t y4,
              uint16_t color) {
  tft.fillTriangle(x1, y1, x2, y2, x3, y3, color);
  tft.fillTriangle(x1, y1, x3, y3, x4, y4, color);
}

int16_t screenX(float x) {
  if (x < -32760.0f) return -32760;
  if (x > 32760.0f) return 32760;
  return static_cast<int16_t>(x);
}

int16_t screenY(float y) {
  if (y < -32760.0f) return -32760;
  if (y > 32760.0f) return 32760;
  return static_cast<int16_t>(y);
}

void drawMountains() {
  const int16_t horizon = SCREEN_H / 2;
  tft.fillTriangle(SCREEN_W / 2, horizon - 48,
                   SCREEN_W / 2 - 72, horizon + 2,
                   SCREEN_W / 2 + 72, horizon + 2, MOUNTAIN);
  tft.fillTriangle(SCREEN_W / 2, horizon - 48,
                   SCREEN_W / 2 - 22, horizon - 30,
                   SCREEN_W / 2 + 22, horizon - 30, SNOW);
  tft.fillTriangle(52, horizon - 28, 8, horizon + 2, 104, horizon + 2,
                   MOUNTAIN);
  tft.fillTriangle(SCREEN_W - 40, horizon - 26, SCREEN_W - 112,
                   horizon + 2, SCREEN_W + 22, horizon + 2, MOUNTAIN);
}

void drawSegment(uint16_t index, const Projected& nearPoint,
                 const Projected& farPoint) {
  int16_t yFar = screenY(farPoint.y);
  int16_t yNear = screenY(nearPoint.y);
  if (yFar < 0) yFar = 0;
  if (yNear > SCREEN_H) yNear = SCREEN_H;
  if (yFar >= yNear) return;

  const uint16_t stripe = (index / 3) % 2;
  const uint16_t grass = stripe ? GRASS_LIGHT : GRASS_DARK;
  const uint16_t road = stripe ? ROAD_LIGHT : ROAD_DARK;
  const uint16_t rumble = stripe ? RUMBLE_LIGHT : RUMBLE_DARK;
  tft.fillRect(0, yFar, SCREEN_W, yNear - yFar, grass);

  const float nearLeft = nearPoint.x - nearPoint.halfWidth;
  const float nearRight = nearPoint.x + nearPoint.halfWidth;
  const float farLeft = farPoint.x - farPoint.halfWidth;
  const float farRight = farPoint.x + farPoint.halfWidth;
  const float nearRumble = fmaxf(1.0f, nearPoint.halfWidth * 0.12f);
  const float farRumble = fmaxf(1.0f, farPoint.halfWidth * 0.12f);

  fillQuad(screenX(farLeft), yFar, screenX(farRight), yFar,
           screenX(nearRight), yNear, screenX(nearLeft), yNear, road);
  fillQuad(screenX(farLeft - farRumble), yFar,
           screenX(farLeft), yFar,
           screenX(nearLeft), yNear,
           screenX(nearLeft - nearRumble), yNear, rumble);
  fillQuad(screenX(farRight), yFar,
           screenX(farRight + farRumble), yFar,
           screenX(nearRight + nearRumble), yNear,
           screenX(nearRight), yNear, rumble);

  if (stripe == 0) {
    const float nearDash = fmaxf(1.0f, nearPoint.halfWidth * 0.05f);
    const float farDash = fmaxf(1.0f, farPoint.halfWidth * 0.05f);
    fillQuad(screenX(farPoint.x - farDash), yFar,
             screenX(farPoint.x + farDash), yFar,
             screenX(nearPoint.x + nearDash), yNear,
             screenX(nearPoint.x - nearDash), yNear, LANE);
  }
}

void drawCar() {
  const int16_t centerX = SCREEN_W / 2 +
      static_cast<int16_t>(playerX * (SCREEN_W * 0.30f));
  const int16_t centerY = SCREEN_H - 45;
  tft.fillRect(centerX - 30, centerY + 17, 60, 4, ROAD_DARK);
  tft.fillRect(centerX - 30, centerY - 8, 8, 22, CAR_TIRE);
  tft.fillRect(centerX + 22, centerY - 8, 8, 22, CAR_TIRE);
  tft.fillRect(centerX - 26, centerY - 12, 52, 28, CAR_BODY);
  tft.fillRect(centerX - 32, centerY - 16, 64, 6, CAR_WING);
  tft.fillRect(centerX - 14, centerY - 8, 28, 12, CAR_GLASS);
  tft.fillRect(centerX - 22, centerY + 6, 10, 4, CAR_LIGHT);
  tft.fillRect(centerX + 12, centerY + 6, 10, 4, CAR_LIGHT);
  tft.fillRect(centerX - 2, centerY - 12, 4, 28, CAR_LIGHT);
}

void drawTrafficCar(const TrafficCar& car, float playerSegmentFraction) {
  if (car.distanceAhead <= 0.0f) return;
  const float segmentPosition =
      car.distanceAhead / SEGMENT_LENGTH + playerSegmentFraction;
  const uint16_t segment = static_cast<uint16_t>(segmentPosition);
  if (segment >= DRAW_DISTANCE) return;
  const float fraction = segmentPosition - segment;
  const Projected& nearPoint = nearPoints[segment];
  const Projected& farPoint = farPoints[segment];
  const float halfWidth = nearPoint.halfWidth +
                          (farPoint.halfWidth - nearPoint.halfWidth) * fraction;
  const float centerX = nearPoint.x + (farPoint.x - nearPoint.x) * fraction +
                        car.lane * halfWidth;
  const float centerY = nearPoint.y + (farPoint.y - nearPoint.y) * fraction;
  if (centerY < 48.0f || centerY > SCREEN_H - 34.0f) return;

  int16_t width = static_cast<int16_t>(nearPoint.halfWidth * 0.28f);
  if (width < 5) width = 5;
  if (width > 28) width = 28;
  const int16_t height = static_cast<int16_t>(width * 0.72f);
  const int16_t x = screenX(centerX);
  const int16_t y = screenY(centerY);

  tft.fillRect(x - width / 2, y - height, width, height, car.color);
  tft.fillRect(x - width / 3, y - height - height / 3,
               width * 2 / 3, height / 3, car.color);
  tft.fillRect(x - width / 2 - 1, y - 3, 2, 4, CAR_TIRE);
  tft.fillRect(x + width / 2 - 1, y - 3, 2, 4, CAR_TIRE);
  tft.fillRect(x - width / 3, y - height / 2, width * 2 / 3, 2, CAR_GLASS);
  tft.fillRect(x - width / 3, y - 3, 2, 2, CAR_LIGHT);
  tft.fillRect(x + width / 3 - 2, y - 3, 2, 2, CAR_LIGHT);
}

void drawRace() {
  if (frameBuffer == nullptr || track == nullptr || nearPoints == nullptr ||
      farPoints == nullptr) return;
  const float length = trackLength();
  float wrappedPosition = fmodf(position, length);
  if (wrappedPosition < 0.0f) wrappedPosition += length;
  const uint16_t base = segmentAt(wrappedPosition);
  const float segmentPosition = wrappedPosition / SEGMENT_LENGTH;
  const float percent = segmentPosition - floorf(segmentPosition);
  const uint16_t next = (base + 1) % trackCount();
  const float playerY = track[base].y + (track[next].y - track[base].y) * percent;
  const float cameraX = playerX * ROAD_WIDTH;
  const float cameraY = playerY + CAMERA_HEIGHT;

  float worldX = 0.0f;
  float deltaX = -track[base].curve * percent;
  float topRoadY = static_cast<float>(SCREEN_H);
  for (uint16_t n = 0; n < DRAW_DISTANCE; ++n) {
    const uint16_t i = (base + n) % trackCount();
    const uint16_t j = (base + n + 1) % trackCount();
    const float nearZ = n * SEGMENT_LENGTH - percent * SEGMENT_LENGTH;
    const float farZ = (n + 1) * SEGMENT_LENGTH - percent * SEGMENT_LENGTH;
    project(nearPoints[n], worldX, track[i].y, cameraX, cameraY, nearZ);
    project(farPoints[n], worldX + deltaX, track[j].y,
            cameraX, cameraY, farZ);
    if (farPoints[n].y < topRoadY) topRoadY = farPoints[n].y;
    worldX += deltaX;
    deltaX += track[i].curve;
  }

  int16_t skyHeight = screenY(lroundf(topRoadY));
  if (skyHeight < 0) skyHeight = 0;
  if (skyHeight > SCREEN_H) skyHeight = SCREEN_H;
  tft.fillScreen(SKY);
  tft.fillRect(0, 0, SCREEN_W, skyHeight, SKY);
  drawMountains();
  for (int16_t n = DRAW_DISTANCE - 1; n >= 0; --n) {
    drawSegment((base + n) % trackCount(), nearPoints[n], farPoints[n]);
  }
  for (uint8_t i = 0; i < trafficCount; ++i) {
    drawTrafficCar(traffic[i], percent);
  }
  drawCar();

  char text[16];
  const int32_t shownLap = (lap < TOTAL_LAPS) ? lap + 1 : TOTAL_LAPS;
  if (lapFlashMs && millis() - lapFlashMs < 1500) {
    snprintf(text, sizeof(text), arcadeText("OKR %ld!", "LAP %ld!"),
             static_cast<long>(shownLap));
  } else {
    snprintf(text, sizeof(text), arcadeText("OKR %ld/%u", "LAP %ld/%u"),
             static_cast<long>(shownLap), TOTAL_LAPS);
  }
  drawHudBox(2, 3, 55, 20, text);
  snprintf(text, sizeof(text), arcadeText("PKT %ld", "PTS %ld"),
           static_cast<long>(score));
  drawHudBox(60, 3, 55, 20, text);
  snprintf(text, sizeof(text), arcadeText("C %lds", "T %lds"),
           static_cast<long>(timeLeftMs / 1000));
  drawHudBox(118, 3, 55, 20, text);
  snprintf(text, sizeof(text), arcadeText("PRED %d", "SPD %d"),
           static_cast<int>(speed / 10.0f));
  drawHudBox(176, 3, 62, 20, text);
  panel.drawRGBBitmap(0, 0, frameBuffer, SCREEN_W, SCREEN_H);
}

void drawHudBox(int16_t x, int16_t y, int16_t width, int16_t height,
                const char* text) {
  tft.fillRect(x, y, width, height, ST77XX_BLACK);
  tft.drawRect(x, y, width, height, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setTextWrap(false);
  const int16_t textWidth = static_cast<int16_t>(strlen(text) * 6);
  tft.setCursor(x + (width - textWidth) / 2, y + 7);
  tft.print(text);
}

void drawMenu() {
  tft.fillScreen(SKY);
  drawMountains();
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(2);
  tft.setTextWrap(false);
  tft.setCursor(42, 18);
  tft.print("YELLOW");
  tft.setTextColor(ST77XX_YELLOW);
  tft.setCursor(64, 38);
  tft.print("RACER");
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_WHITE);
  tft.setCursor(45, 70);
  tft.print(arcadeText("WYBIERZ TRASE", "SELECT A TRACK"));

  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t y = 98 + i * 51;
    const uint16_t fill = (i == selectedTrack) ? ST77XX_YELLOW : MENU_BUTTON;
    const uint16_t ink = (i == selectedTrack) ? ST77XX_BLACK : ST77XX_WHITE;
    tft.fillRoundRect(24, y, 192, 40, 5, fill);
    tft.drawRoundRect(24, y, 192, 40, 5, ST77XX_WHITE);
    tft.setTextColor(ink);
    tft.setTextSize(2);
    tft.setCursor(90, y + 12);
    tft.print(TRACKS[i].name);
    if (i == selectedTrack) {
      tft.fillTriangle(42, y + 12, 42, y + 28, 54, y + 20, ST77XX_BLACK);
    }
    tft.setTextSize(1);
    tft.setTextColor(ink);
    tft.setCursor(145, y + 16);
    tft.print(arcadeText("REK ", "BEST "));
    tft.print(highScores[i]);
  }
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(26, 270);
  tft.print(arcadeText("G/D: TRASA   JOY: START",
                       "UP/DOWN: TRACK   JOY: START"));
  tft.setCursor(56, 288);
  tft.print(arcadeText("Z: WYJSCIE   X: INWERSJA",
                       "Z: QUIT   X: INVERT"));
  panel.drawRGBBitmap(0, 0, frameBuffer, SCREEN_W, SCREEN_H);
}

void drawGameOver() {
  tft.fillScreen(ST77XX_BLACK);
  tft.setTextColor(won ? ST77XX_GREEN : ST77XX_RED);
  tft.setTextSize(2);
  tft.setTextWrap(false);
  tft.setCursor(43, 66);
  tft.print(crashed ? arcadeText("KRACHA!", "CRASH!")
                    : won ? arcadeText("META!", "FINISH!")
                          : arcadeText("CZAS!", "TIME UP"));
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(83, 116);
  tft.print(arcadeText("WYNIK", "SCORE"));
  tft.setTextSize(2);
  tft.setCursor(94, 136);
  tft.print(score);
  tft.setTextSize(1);
  tft.setCursor(76, 164);
  tft.print(arcadeText("REK ", "BEST "));
  tft.print(highScores[selectedTrack]);
  tft.setTextSize(1);
  tft.setCursor(46, 190);
  tft.print(crashed
      ? arcadeText("A: PONOW   JOY: MENU", "A: RETRY   JOY: MENU")
      : arcadeText("JOY: POWROT DO MENU", "JOY: BACK TO MENU"));
  panel.drawRGBBitmap(0, 0, frameBuffer, SCREEN_W, SCREEN_H);
}

void startRace(uint8_t trackId) {
  selectedTrack = trackId % 3;
  buildTrack(selectedTrack);
  position = 0.0f;
  speed = 0.0f;
  playerX = 0.0f;
  lap = 0;
  score = 0;
  crashed = false;
  lapFlashMs = 0;
  timeLeftMs = TRACKS[selectedTrack].timeBudget * 1000;
  trafficCount = 2 + selectedTrack * 2;
  for (uint8_t i = 0; i < trafficCount; ++i) {
    traffic[i] = {
        3200.0f + i * (10500.0f / trafficCount) + random(0, 2200),
        3000.0f + random(0, 2200),
        static_cast<float>(static_cast<int>(random(0, 3)) - 1) * 0.52f,
        static_cast<uint16_t>(i % 3 == 0 ? 0xF800 : (i % 3 == 1 ? 0x07FF : 0xFD20)),
    };
  }
  state = State::Race;
  lastTickMs = millis();
  drawRace();
  arcadePlaySound(1000, 45);
}

void finishRace(bool didWin, bool didCrash = false) {
  won = didWin;
  crashed = didCrash;
  if (score > 0 && static_cast<uint32_t>(score) > highScores[selectedTrack]) {
    highScores[selectedTrack] = static_cast<uint32_t>(score);
    const char* keys[] = {"racer_e", "racer_m", "racer_h"};
    arcadeStoreHighScore(keys[selectedTrack], highScores[selectedTrack]);
  }
  state = State::GameOver;
  arcadePlaySound(didWin ? 1500 : 220, 120);
  drawGameOver();
}

void updateMenu(uint32_t now) {
  if (now - lastMenuInputMs < 180) return;
  int8_t direction = 0;
  if (digitalRead(pins::joy_up) == LOW ||
      digitalRead(pins::joy_left) == LOW) {
    direction = -1;
  } else if (digitalRead(pins::joy_down) == LOW ||
             digitalRead(pins::joy_right) == LOW) {
    direction = 1;
  }
  if (direction != 0) {
    selectedTrack = static_cast<uint8_t>((selectedTrack + 3 + direction) % 3);
    lastMenuInputMs = now;
    drawMenu();
    arcadePlaySound(720 + selectedTrack * 120, 25);
    return;
  }
  const bool joyPressed = digitalRead(pins::joy_button) == LOW;
  if (joyPressed && !previousJoyButton) {
    startRace(selectedTrack);
    lastMenuInputMs = now;
  }
}

void updateRace(uint32_t now) {
  float dt = (now - lastTickMs) / 1000.0f;
  lastTickMs = now;
  if (dt > 0.1f) dt = 0.1f;
  if (dt < 0.0f) dt = 0.0f;

  const bool offRoad = playerX < -1.0f || playerX > 1.0f;
  if (offRoad) {
    speed -= OFFROAD_DRAG * dt;
    if (speed < OFFROAD_MIN_SPEED) speed = OFFROAD_MIN_SPEED;
  } else {
    speed += (MAX_SPEED - speed) * 0.9f * dt;
    if (speed > MAX_SPEED) speed = MAX_SPEED;
  }

  if (digitalRead(pins::joy_left) == LOW) {
    playerX += (-1.0f - playerX) * STEER_SPEED * dt;
  } else if (digitalRead(pins::joy_right) == LOW) {
    playerX += (1.0f - playerX) * STEER_SPEED * dt;
  }
  playerX -= curveAt(position) * (speed / MAX_SPEED) * CENTRIFUGAL * dt;
  if (playerX < -PLAYER_X_MAX) playerX = -PLAYER_X_MAX;
  if (playerX > PLAYER_X_MAX) playerX = PLAYER_X_MAX;

  for (uint8_t i = 0; i < trafficCount; ++i) {
    const float previousDistance = traffic[i].distanceAhead;
    float closingSpeed = speed - traffic[i].speed;
    if (closingSpeed < -500.0f) closingSpeed = -500.0f;
    traffic[i].distanceAhead -= closingSpeed * dt;

    const float currentDistance = traffic[i].distanceAhead;
    const bool overlapsCarRange =
        fminf(previousDistance, currentDistance) <= TRAFFIC_COLLISION_RANGE &&
        fmaxf(previousDistance, currentDistance) >= -TRAFFIC_COLLISION_RANGE;
    const bool sameLane =
        fabsf(playerX - traffic[i].lane) <= TRAFFIC_COLLISION_LANE;
    if (overlapsCarRange && sameLane) {
      finishRace(false, true);
      return;
    }

    if (previousDistance > 0.0f && currentDistance <= 0.0f) {
      ++score;
      arcadePlaySound(1250, 35);
    }

    if (traffic[i].distanceAhead < -300.0f) {
      traffic[i].distanceAhead = 9000.0f + random(0, 13000);
      traffic[i].speed = 3000.0f + random(0, 2200);
      traffic[i].lane =
          static_cast<float>(static_cast<int>(random(0, 3)) - 1) * 0.52f;
    }
  }

  position += speed * dt;
  while (position >= trackLength()) {
    position -= trackLength();
    ++lap;
    lapFlashMs = now;
    if (lap >= TOTAL_LAPS) {
      finishRace(true);
      return;
    }
  }

  timeLeftMs -= static_cast<int32_t>(dt * 1000.0f);
  if (timeLeftMs <= 0) {
    timeLeftMs = 0;
    finishRace(false);
  }
}

}  // namespace

void begin() {
  state = State::Menu;
  selectedTrack = 0;
  const char* scoreKeys[] = {"racer_e", "racer_m", "racer_h"};
  for (uint8_t i = 0; i < 3; ++i) {
    highScores[i] = arcadeLoadUInt(scoreKeys[i], 0);
  }
  previousInvertButton = false;
  previousExitButton = false;
  previousJoyButton = digitalRead(pins::joy_button) == LOW;
  previousRestartButton = digitalRead(pins::button_p1) == LOW;
  lastTickMs = millis();
  lastFrameMs = 0;
  lastMenuInputMs = 0;

  SPI.begin(pins::tft_clk, pins::tft_miso, pins::tft_mosi, pins::tft_cs);
  panel.init(SCREEN_W, SCREEN_H);
  panel.setRotation(0);
  panel.invertDisplay(arcadeDisplayInverted());

  pinMode(pins::joy_up, INPUT_PULLUP);
  pinMode(pins::joy_down, INPUT_PULLUP);
  pinMode(pins::joy_left, INPUT_PULLUP);
  pinMode(pins::joy_right, INPUT_PULLUP);
  pinMode(pins::joy_button, INPUT_PULLUP);
  pinMode(pins::button_p3, INPUT_PULLUP);
  pinMode(pins::button_p4, INPUT_PULLUP);

  if (frameBuffer == nullptr) {
    frameBuffer = static_cast<uint16_t*>(heap_caps_malloc(
        static_cast<size_t>(SCREEN_W) * SCREEN_H * sizeof(*frameBuffer),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }

  if (track == nullptr) {
    track = static_cast<Segment*>(heap_caps_malloc(
        TRACK_SEGMENTS * sizeof(*track), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (nearPoints == nullptr) {
    nearPoints = static_cast<Projected*>(heap_caps_malloc(
        DRAW_DISTANCE * sizeof(*nearPoints), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (farPoints == nullptr) {
    farPoints = static_cast<Projected*>(heap_caps_malloc(
        DRAW_DISTANCE * sizeof(*farPoints), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (traffic == nullptr) {
    traffic = static_cast<TrafficCar*>(
        heap_caps_malloc(6 * sizeof(*traffic), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (frameBuffer == nullptr || track == nullptr || nearPoints == nullptr ||
      farPoints == nullptr || traffic == nullptr) {
    Serial.println("Yellow Racer startup error: unable to allocate render data in PSRAM.");
    panel.fillScreen(ST77XX_BLACK);
    panel.setTextColor(ST77XX_RED);
    panel.setCursor(12, 120);
    panel.println(arcadeText("BRAK PAMIECI", "OUT OF MEMORY"));
    return;
  }
  tft.setBuffer(frameBuffer);
  tft.setRotation(0);

  drawMenu();
}

void tick() {
  const uint32_t now = millis();
  const bool invertPressed = digitalRead(pins::button_p3) == LOW;
  if (invertPressed && !previousInvertButton) {
    arcadeToggleDisplayInversion();
    panel.invertDisplay(arcadeDisplayInverted());
  }
  previousInvertButton = invertPressed;

  const bool exitPressed = digitalRead(pins::button_p4) == LOW;
  const bool exitPressedEdge = exitPressed && !previousExitButton;
  previousExitButton = exitPressed;
  if (exitPressedEdge) {
    arcadeRequestGameExit();
    return;
  }
  if (frameBuffer == nullptr || track == nullptr || nearPoints == nullptr ||
      farPoints == nullptr || traffic == nullptr) return;

  const bool joyPressed = digitalRead(pins::joy_button) == LOW;
  const bool restartPressed = digitalRead(pins::button_p1) == LOW;
  const bool restartPressedEdge = restartPressed && !previousRestartButton;
  if (state == State::Menu) {
    updateMenu(now);
  } else if (state == State::Race) {
    updateRace(now);
  } else if (state == State::GameOver && crashed && restartPressedEdge) {
    startRace(selectedTrack);
  } else if (joyPressed && !previousJoyButton) {
    state = State::Menu;
    drawMenu();
  }
  previousJoyButton = joyPressed;
  previousRestartButton = restartPressed;

  if (state == State::Race && now - lastFrameMs >= 40) {
    drawRace();
    lastFrameMs = now;
  }
}

}  // namespace yellowracer
