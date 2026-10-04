#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "arcade_persistence.h"
#include "arcade_runtime.h"
#include "pins.h"

namespace pomodoro {
namespace {

constexpr uint8_t kOptionCount = 5;
constexpr uint8_t kMinDurationMinutes = 1;
constexpr uint8_t kMaxWorkMinutes = 120;
constexpr uint8_t kMaxBreakMinutes = 60;
constexpr uint8_t kMaxShortSessions = 12;
constexpr uint32_t kButtonDebounceMs = 200;
constexpr uint32_t kPersistIntervalMs = 5UL * 60UL * 1000UL;
constexpr uint16_t kDarkGray = 0x7BEF;
constexpr uint16_t kLightGray = 0xC618;

Adafruit_ST7789 display(&SPI, pins::tft_cs, pins::tft_dc, pins::tft_rst);

enum class Screen : uint8_t { Timer, Settings, ResetConfirm };
Screen screen = Screen::Timer;

uint8_t workDurationMinutes = 25;
uint8_t breakDurationMinutes = 5;
uint8_t shortSessionsTarget = 4;
uint8_t settingsCursor = 0;
uint32_t timeLeftSeconds = 25 * 60;
uint32_t totalFocusSeconds = 0;
uint32_t longSessionsCount = 0;
uint32_t shortSessionsCount = 0;
uint8_t currentShortSessionIndex = 0;
uint32_t lastTickMs = 0;
uint32_t lastInputMs = 0;
uint32_t lastPersistenceMs = 0;
bool soundEnabled = true;
bool isWorkSession = true;
bool isRunning = false;
bool previousUp = false;
bool previousDown = false;
bool previousP1 = false;
bool previousP2 = false;
bool previousP3 = false;
bool previousP4 = false;

bool persistState() {
  bool success = true;
  success &= arcadeStoreUInt("pom_focus", totalFocusSeconds);
  success &= arcadeStoreUInt("pom_long", longSessionsCount);
  success &= arcadeStoreUInt("pom_short", shortSessionsCount);
  success &= arcadeStoreUInt("pom_cycle", currentShortSessionIndex);
  success &= arcadeStoreUInt("pom_work_min", workDurationMinutes);
  success &= arcadeStoreUInt("pom_break_min", breakDurationMinutes);
  success &= arcadeStoreUInt("pom_target", shortSessionsTarget);
  success &= arcadeStoreUInt("pom_sound", soundEnabled ? 1 : 0);
  success &= arcadeStoreUInt("pom_working", isWorkSession ? 1 : 0);
  success &= arcadeStoreUInt("pom_remaining", timeLeftSeconds);
  lastPersistenceMs = millis();
  return success;
}

void loadState() {
  totalFocusSeconds = arcadeLoadUInt("pom_focus", 0);
  longSessionsCount = arcadeLoadUInt("pom_long", 0);
  shortSessionsCount = arcadeLoadUInt("pom_short", 0);
  const uint32_t storedWorkMinutes = arcadeLoadUInt("pom_work_min", 25);
  const uint32_t storedBreakMinutes = arcadeLoadUInt("pom_break_min", 5);
  const uint32_t storedTarget = arcadeLoadUInt("pom_target", 4);
  soundEnabled = arcadeLoadUInt("pom_sound", 1) != 0;
  isWorkSession = arcadeLoadUInt("pom_working", 1) != 0;
  workDurationMinutes =
      storedWorkMinutes >= kMinDurationMinutes && storedWorkMinutes <= kMaxWorkMinutes
          ? static_cast<uint8_t>(storedWorkMinutes)
          : 25;
  breakDurationMinutes =
      storedBreakMinutes >= kMinDurationMinutes && storedBreakMinutes <= kMaxBreakMinutes
          ? static_cast<uint8_t>(storedBreakMinutes)
          : 5;
  shortSessionsTarget =
      storedTarget >= 1 && storedTarget <= kMaxShortSessions
          ? static_cast<uint8_t>(storedTarget)
          : 4;
  currentShortSessionIndex = static_cast<uint8_t>(
      arcadeLoadUInt("pom_cycle", 0) % shortSessionsTarget);
  const uint32_t configuredTime =
      static_cast<uint32_t>(isWorkSession ? workDurationMinutes : breakDurationMinutes) * 60;
  timeLeftSeconds = arcadeLoadUInt("pom_remaining", configuredTime);
  if (timeLeftSeconds > configuredTime) timeLeftSeconds = configuredTime;
  isRunning = false;
  lastPersistenceMs = millis();
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

void playTone(uint16_t frequency, uint16_t durationMs) {
  if (soundEnabled) arcadePlaySound(frequency, durationMs);
}

void syncButtonState() {
  previousUp = pressed(pins::joy_up);
  previousDown = pressed(pins::joy_down);
  previousP1 = pressed(pins::button_p1);
  previousP2 = pressed(pins::button_p2);
  previousP3 = pressed(pins::button_p3);
  previousP4 = pressed(pins::button_p4);
}

void drawHeader() {
  display.fillRect(0, 0, 240, 28, kDarkGray);
  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE, kDarkGray);
  display.setCursor(7, 6);
  display.print(arcadeText("SKUPIENIE ", "FOCUS "));
  display.print(totalFocusSeconds / 60);
  display.print("m");
  display.setCursor(126, 6);
  display.print(arcadeText("CYKLE ", "CYCLES "));
  display.print(longSessionsCount);
  display.print("/");
  display.print(shortSessionsCount);
}

void drawTimer() {
  display.fillScreen(ST77XX_BLACK);
  drawHeader();

  display.setTextSize(2);
  display.setTextColor(isWorkSession ? ST77XX_GREEN : ST77XX_CYAN);
  display.setCursor(30, 48);
  display.print(isWorkSession
      ? arcadeText("CZAS PRACY", "WORK TIME")
      : arcadeText("PRZERWA", "BREAK"));

  display.setTextSize(1);
  display.setTextColor(kLightGray);
  display.setCursor(24, 82);
  display.print(arcadeText("Do dlugiej przerwy: ", "Until long break: "));
  display.print(shortSessionsTarget - currentShortSessionIndex);

  char clockText[8];
  snprintf(clockText, sizeof(clockText), "%02lu:%02lu",
           static_cast<unsigned long>(timeLeftSeconds / 60),
           static_cast<unsigned long>(timeLeftSeconds % 60));
  display.setTextSize(4);
  display.setTextColor(ST77XX_YELLOW);
  int16_t textX, textY;
  uint16_t textWidth, textHeight;
  display.getTextBounds(clockText, 0, 0, &textX, &textY, &textWidth, &textHeight);
  display.setCursor((240 - textWidth) / 2, 130);
  display.print(clockText);

  display.setTextSize(2);
  display.setTextColor(isRunning ? ST77XX_GREEN : ST77XX_ORANGE);
  display.setCursor(86, 194);
  display.print(isRunning ? arcadeText("START", "RUNNING")
                          : arcadeText("PAUZA", "PAUSED"));

  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(8, 263);
  display.print(arcadeText("B: Start / pauza", "B: Start / pause"));
  display.setCursor(8, 281);
  display.print(arcadeText("A: Ustawienia   Z: Menu",
                           "A: Settings   Z: Menu"));
  display.setCursor(8, 299);
  display.setTextColor(ST77XX_CYAN);
  display.print(arcadeText("X: Odwroc kolory", "X: Invert colors"));
}

void drawSettings() {
  display.fillScreen(ST77XX_BLACK);
  display.setTextSize(2);
  display.setTextColor(ST77XX_MAGENTA);
  display.setCursor(36, 17);
  display.print(arcadeText("USTAWIENIA", "SETTINGS"));
  display.drawFastHLine(12, 45, 216, ST77XX_MAGENTA);

  const char* labels[kOptionCount] = {
      arcadeText("Dzwiek", "Sound"),
      arcadeText("Czas pracy", "Work duration"),
      arcadeText("Przerwa", "Break duration"),
      arcadeText("Sesje w cyklu", "Sessions per cycle"),
      arcadeText("Reset statystyk", "Reset statistics"),
  };
  char value[16];
  for (uint8_t i = 0; i < kOptionCount; ++i) {
    const int16_t y = 57 + i * 38;
    display.fillRect(10, y - 4, 220, 32,
                     i == settingsCursor ? kDarkGray : ST77XX_BLACK);
    display.setTextSize(1);
    display.setTextColor(i == settingsCursor ? ST77XX_YELLOW : ST77XX_WHITE);
    display.setCursor(18, y + 4);
    display.print(i == settingsCursor ? "> " : "  ");
    display.print(labels[i]);

    if (i == 0) {
      snprintf(value, sizeof(value), "%s",
               soundEnabled ? arcadeText("WL", "ON")
                            : arcadeText("WYL", "OFF"));
    } else if (i == 1) {
      snprintf(value, sizeof(value), "%um", workDurationMinutes);
    } else if (i == 2) {
      snprintf(value, sizeof(value), "%um", breakDurationMinutes);
    } else if (i == 3) {
      snprintf(value, sizeof(value), "%u", shortSessionsTarget);
    } else {
      value[0] = '\0';
    }
    display.setCursor(154, y + 4);
    display.print(value);
  }

  display.setTextSize(1);
  display.setTextColor(kLightGray);
  display.setCursor(8, 263);
  display.print(arcadeText("Gora / dol: wybor", "Up / down: select"));
  display.setCursor(8, 281);
  display.print(arcadeText("X / Z: zmiana wartosci",
                           "X / Z: change value"));
  display.setCursor(8, 299);
  display.print(arcadeText("A: Zapisz / resetuj", "A: Save / reset"));
}

void drawResetConfirm() {
  display.fillScreen(ST77XX_BLACK);
  display.setTextColor(ST77XX_RED);
  display.setTextSize(2);
  display.setCursor(37, 54);
  display.print(arcadeText("RESET LICZNIKOW", "RESET COUNTERS"));
  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(24, 111);
  display.print(arcadeText("Wyzerowac skupienie i", "Reset focus time and"));
  display.setCursor(24, 130);
  display.print(arcadeText("liczniki zakonczonych sesji?",
                           "completed session counters?"));
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(24, 192);
  display.print(arcadeText("A: Potwierdz", "A: Confirm"));
  display.setCursor(24, 213);
  display.print(arcadeText("Z: Anuluj", "Z: Cancel"));
}

void adjustSetting(bool increase) {
  switch (settingsCursor) {
    case 0:
      soundEnabled = !soundEnabled;
      break;
    case 1:
      if (increase && workDurationMinutes < kMaxWorkMinutes) ++workDurationMinutes;
      else if (!increase && workDurationMinutes > kMinDurationMinutes) --workDurationMinutes;
      break;
    case 2:
      if (increase && breakDurationMinutes < kMaxBreakMinutes) ++breakDurationMinutes;
      else if (!increase && breakDurationMinutes > kMinDurationMinutes) --breakDurationMinutes;
      break;
    case 3:
      if (increase && shortSessionsTarget < kMaxShortSessions) ++shortSessionsTarget;
      else if (!increase && shortSessionsTarget > 1) --shortSessionsTarget;
      break;
  }
}

void updateTimer(uint32_t now);

void enterSettings(uint32_t now) {
  if (isRunning) updateTimer(now);
  isRunning = false;
  screen = Screen::Settings;
  settingsCursor = 0;
  lastTickMs = now;
  persistState();
  playTone(1500, 60);
  drawSettings();
}

void leaveSettings(uint32_t now) {
  timeLeftSeconds = static_cast<uint32_t>(
      isWorkSession ? workDurationMinutes : breakDurationMinutes) * 60;
  isRunning = false;
  screen = Screen::Timer;
  lastTickMs = now;
  persistState();
  playTone(1800, 70);
  drawTimer();
}

void finishInterval() {
  playTone(2000, 120);
  if (isWorkSession) {
    ++shortSessionsCount;
    ++currentShortSessionIndex;
    if (currentShortSessionIndex >= shortSessionsTarget) {
      ++longSessionsCount;
      currentShortSessionIndex = 0;
    }
    isWorkSession = false;
    timeLeftSeconds = static_cast<uint32_t>(breakDurationMinutes) * 60;
  } else {
    isWorkSession = true;
    timeLeftSeconds = static_cast<uint32_t>(workDurationMinutes) * 60;
  }
  persistState();
}

void updateTimer(uint32_t now) {
  if (!isRunning) return;

  const uint32_t elapsedSeconds = (now - lastTickMs) / 1000;
  if (elapsedSeconds == 0) return;
  lastTickMs += elapsedSeconds * 1000;

  uint32_t remainingElapsed = elapsedSeconds;
  while (remainingElapsed > 0 && isRunning) {
    if (timeLeftSeconds == 0) finishInterval();
    const uint32_t elapsedThisInterval =
        remainingElapsed < timeLeftSeconds ? remainingElapsed : timeLeftSeconds;
    if (isWorkSession) totalFocusSeconds += elapsedThisInterval;
    timeLeftSeconds -= elapsedThisInterval;
    remainingElapsed -= elapsedThisInterval;
  }
  if (timeLeftSeconds == 0) finishInterval();
  if (now - lastPersistenceMs >= kPersistIntervalMs) persistState();
  drawTimer();
}

}  // namespace

void begin() {
  screen = Screen::Timer;
  lastTickMs = millis();
  lastInputMs = lastTickMs;
  loadState();

  SPI.begin(pins::tft_clk, pins::tft_miso, pins::tft_mosi, pins::tft_cs);
  display.init(240, 320);
  display.setRotation(0);
  display.invertDisplay(arcadeDisplayInverted());

  pinMode(pins::joy_up, INPUT_PULLUP);
  pinMode(pins::joy_down, INPUT_PULLUP);
  pinMode(pins::button_p1, INPUT_PULLUP);
  pinMode(pins::button_p2, INPUT_PULLUP);
  pinMode(pins::button_p3, INPUT_PULLUP);
  pinMode(pins::button_p4, INPUT_PULLUP);
  syncButtonState();
  drawTimer();
}

void tick() {
  const uint32_t now = millis();
  const bool p4Edge = pressedEdge(pins::button_p4, previousP4);
  if (screen == Screen::Timer && p4Edge) {
    if (isRunning) updateTimer(now);
    isRunning = false;
    persistState();
    arcadeRequestGameExit();
    return;
  }

  if (now - lastInputMs >= kButtonDebounceMs) {
    bool changed = false;
    if (screen == Screen::Timer) {
      if (pressedEdge(pins::button_p3, previousP3)) {
        arcadeToggleDisplayInversion();
        display.invertDisplay(arcadeDisplayInverted());
        playTone(1100, 35);
        lastInputMs = now;
      } else if (pressedEdge(pins::button_p2, previousP2)) {
        if (isRunning) updateTimer(now);
        isRunning = !isRunning;
        lastTickMs = now;
        persistState();
        playTone(isRunning ? 1000 : 700, 45);
        lastInputMs = now;
        drawTimer();
      } else if (pressedEdge(pins::button_p1, previousP1)) {
        enterSettings(now);
        lastInputMs = now;
      }
    } else if (screen == Screen::Settings) {
      if (pressedEdge(pins::joy_down, previousDown)) {
        settingsCursor = (settingsCursor + 1) % kOptionCount;
        changed = true;
      } else if (pressedEdge(pins::joy_up, previousUp)) {
        settingsCursor = (settingsCursor + kOptionCount - 1) % kOptionCount;
        changed = true;
      } else if (pressedEdge(pins::button_p3, previousP3)) {
        if (settingsCursor == kOptionCount - 1) {
          screen = Screen::ResetConfirm;
          drawResetConfirm();
        } else {
          adjustSetting(true);
          changed = true;
        }
      } else if (p4Edge) {
        if (settingsCursor != kOptionCount - 1) {
          adjustSetting(false);
          changed = true;
        }
      } else if (pressedEdge(pins::button_p1, previousP1)) {
        if (settingsCursor == kOptionCount - 1) {
          screen = Screen::ResetConfirm;
          drawResetConfirm();
        } else {
          leaveSettings(now);
        }
        lastInputMs = now;
      }
      if (changed) {
        playTone(850, 30);
        lastInputMs = now;
        drawSettings();
      }
    } else {
      if (pressedEdge(pins::button_p1, previousP1)) {
        totalFocusSeconds = 0;
        longSessionsCount = 0;
        shortSessionsCount = 0;
        currentShortSessionIndex = 0;
        persistState();
        screen = Screen::Settings;
        playTone(1800, 70);
        drawSettings();
        lastInputMs = now;
      } else if (p4Edge) {
        screen = Screen::Settings;
        playTone(700, 45);
        drawSettings();
        lastInputMs = now;
      }
    }
  }

  if (screen == Screen::Timer) updateTimer(now);
}

}  // namespace pomodoro
