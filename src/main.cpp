#include <Arduino.h>
#include <FS.h>
#include <LittleFS.h>
#include <esp_heap_caps.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "pins.h"
#include "games.h"
#include "arcade_runtime.h"
#include "arcade_persistence.h"
#include "doom_audio_runtime.h"
#include "doomgeneric.h"
#include "doomkeys.h"

extern "C" int DG_InitStateTable(void);

namespace {

constexpr int displayWidth = 240;
constexpr int displayHeight = 320;
constexpr int gameImageWidth = 240;
constexpr int gameImageHeight = 220;
constexpr int gameImageYOffset = (displayHeight - gameImageHeight) / 2;

char doomArgProgram[] = "doom";
char doomArgIwad[] = "-iwad";
char doomArgWadPath[] = "/littlefs/doom1.wad";
char doomArgNoMusic[] = "-nomusic";
char *doomArgs[] = {
    doomArgProgram,
    doomArgIwad,
    doomArgWadPath,
    doomArgNoMusic,
};

Adafruit_ST7789 launcherDisplay(&SPI, pins::tft_cs, pins::tft_dc, pins::tft_rst);
uint16_t *renderBuffer = nullptr;

struct ButtonBinding {
  uint8_t pin;
  unsigned char key;
  bool wasPressed;
};

ButtonBinding bindings[] = {
    {pins::joy_up, KEY_UPARROW, false},
    {pins::joy_down, KEY_DOWNARROW, false},
    {pins::joy_left, KEY_LEFTARROW, false},
    {pins::joy_right, KEY_RIGHTARROW, false},
    {pins::joy_button, KEY_ENTER, false},
    {pins::button_p1, KEY_FIRE, false},
    {pins::button_p2, KEY_USE, false},
};

struct KeyEvent {
  bool pressed;
  unsigned char key;
};

KeyEvent keyEvents[8];
uint8_t eventRead = 0;
uint8_t eventWrite = 0;
bool displayInverted = false;
bool soundsEnabled = true;
uint8_t soundVolume = 4;
bool polishLanguage = true;
bool audioReady = false;
uint32_t soundEndsAtMs = 0;
constexpr uint8_t audioChannel = 7;
bool gameExitRequested = false;
bool filesystemReady = false;
bool doomCreated = false;

enum class AppMode { MainMenu, GameMenu, Settings, Placeholder, Doom, Bagman, Snake, Tetris, Racer, Arkanoid, Pomodoro, Screensaver, Tamagotchi, Sleeping };
AppMode mode = AppMode::MainMenu;
uint8_t menuSelection = 0;
uint8_t gameSelection = 0;
bool previousInvertPressed = false;
bool previousUpPressed = false;
bool previousDownPressed = false;
bool previousSelectPressed = false;
bool previousBackPressed = false;
bool previousLeftPressed = false;
bool previousRightPressed = false;
bool previousP1Pressed = false;
bool previousP2Pressed = false;
uint32_t lastCoinFrameMs = 0;
uint8_t coinFrame = 0;
bool menuSoundsEnabled = true;
bool settingsFromGameMenu = false;
uint8_t settingsSelection = 0;

int16_t menuRowStart(size_t count) {
  return count > 5 ? 65 : 77;
}

int16_t menuRowSpacing(size_t count) {
  return count > 5 ? 35 : 39;
}

void syncDisplayInversion() {
  launcherDisplay.invertDisplay(displayInverted);
}

void playMenuSound(uint16_t frequency, uint16_t durationMs) {
  if (menuSoundsEnabled) arcadePlaySound(frequency, durationMs);
}

void toggleMenuSounds() {
  menuSoundsEnabled = !menuSoundsEnabled;
  arcadeSetSoundEnabled(menuSoundsEnabled);
  arcadeStoreUInt("menu_sound", menuSoundsEnabled ? 1 : 0);
  if (menuSoundsEnabled) arcadePlaySound(1250, 45);
}

void drawBoldCentered(const char *text, int16_t centerX, int16_t y,
                      uint8_t size, uint16_t color) {
  int16_t x1, y1;
  uint16_t width, height;
  launcherDisplay.setTextSize(size);
  launcherDisplay.getTextBounds(text, 0, y, &x1, &y1, &width, &height);
  const int16_t x = centerX - static_cast<int16_t>(width / 2);
  launcherDisplay.setTextColor(color);
  launcherDisplay.setCursor(x, y);
  launcherDisplay.print(text);
  launcherDisplay.setCursor(x + 1, y);
  launcherDisplay.print(text);
}

void drawCoinMarker(uint8_t row, uint8_t frame, int16_t rowStart,
                    int16_t rowSpacing) {
  constexpr int16_t centerX = 19;
  constexpr int16_t widths[] = {15, 13, 10, 6, 3, 6, 10, 13};
  const int16_t centerY = rowStart + row * rowSpacing + 7;
  const int16_t width = widths[frame % (sizeof(widths) / sizeof(widths[0]))];

  launcherDisplay.fillRect(7, centerY - 11, 25, 23, ST77XX_BLACK);
  launcherDisplay.fillRoundRect(centerX - width / 2, centerY - 9, width, 18, 4, ST77XX_YELLOW);
  launcherDisplay.drawRoundRect(centerX - width / 2, centerY - 9, width, 18, 4, ST77XX_ORANGE);
  if (width > 5) launcherDisplay.drawFastVLine(centerX, centerY - 6, 12, ST77XX_WHITE);
}

void updateCoinMarker() {
  if (mode != AppMode::MainMenu && mode != AppMode::GameMenu) return;
  const uint32_t now = millis();
  if (now - lastCoinFrameMs < 75) return;
  lastCoinFrameMs = now;
  coinFrame = (coinFrame + 1) % 8;
  const size_t count = mode == AppMode::MainMenu
                           ? 5
                           : 6;
  drawCoinMarker(mode == AppMode::MainMenu ? menuSelection : gameSelection,
                 coinFrame, menuRowStart(count), menuRowSpacing(count));
}

void drawMenu(const char *title, const char *const *items, size_t count,
              uint8_t selected, bool gameMenu) {
  launcherDisplay.fillScreen(ST77XX_BLACK);
  drawBoldCentered(title, displayWidth / 2, 13, 3, ST77XX_CYAN);
  launcherDisplay.drawFastHLine(24, 51, displayWidth - 48, ST77XX_ORANGE);

  const int16_t rowStart = menuRowStart(count);
  const int16_t rowSpacing = menuRowSpacing(count);
  for (size_t i = 0; i < count; ++i) {
    const int16_t y = rowStart + static_cast<int16_t>(i) * rowSpacing;
    drawBoldCentered(items[i], 132, y, 2, ST77XX_WHITE);
  }
  drawCoinMarker(selected, coinFrame, rowStart, rowSpacing);

  launcherDisplay.setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  launcherDisplay.setTextSize(1);
  launcherDisplay.setCursor(8, displayHeight - 35);
  launcherDisplay.print(arcadeText("JOY: WYBOR", "JOY: SELECT"));
  launcherDisplay.print("  B: ");
  launcherDisplay.print(arcadeText("USTAWIENIA", "SETTINGS"));
  launcherDisplay.setCursor(8, displayHeight - 18);
  launcherDisplay.print(gameMenu
      ? arcadeText("X: INWERSJA   Z: POWROT", "X: INVERT   Z: BACK")
      : arcadeText("X: INWERSJA", "X: INVERT"));
  syncDisplayInversion();
}

void drawMainMenu() {
  static const char *const polishItems[] = {
      "Gry", "Tamagochi", "Pomodoro", "Wygaszacz ekranu", "Uspij",
  };
  static const char *const englishItems[] = {
      "Games", "Tamagotchi", "Pomodoro", "Screensaver", "Sleep",
  };
  mode = AppMode::MainMenu;
  const char *const *items = polishLanguage ? polishItems : englishItems;
  drawMenu("ARCADE", items, 5,
           menuSelection, false);
}

void drawGameMenu() {
  static const char *const items[] = {
      "Doom", "Bag-Man", "Snake", "Tetris", "Yellow Racer", "Arkanoid",
  };
  mode = AppMode::GameMenu;
  drawMenu(arcadeText("WYBIERZ GRE", "SELECT A GAME"), items,
           sizeof(items) / sizeof(items[0]), gameSelection, true);
}

void drawSettings() {
  mode = AppMode::Settings;
  launcherDisplay.fillScreen(ST77XX_BLACK);
  drawBoldCentered(arcadeText("USTAWIENIA", "SETTINGS"), displayWidth / 2,
                   22, 2, ST77XX_CYAN);
  launcherDisplay.drawFastHLine(24, 54, displayWidth - 48, ST77XX_ORANGE);

  const char *labels[] = {
      arcadeText("Dzwiek", "Sound"),
      arcadeText("Glosnosc", "Volume"),
      arcadeText("Jezyk", "Language"),
      arcadeText("Podswietlenie", "Backlight"),
  };
  const char *values[] = {
      menuSoundsEnabled ? arcadeText("WLACZONY", "ON")
                        : arcadeText("WYLACZONY", "OFF"),
      soundVolume == 1 ? "25%" : soundVolume == 2 ? "50%"
                        : soundVolume == 3 ? "75%" : "100%",
      polishLanguage ? "POLSKI" : "ENGLISH",
      arcadeText("BRAK GPIO", "NOT WIRED"),
  };
  for (uint8_t i = 0; i < 4; ++i) {
    const int16_t y = 78 + i * 43;
    if (i == settingsSelection) {
      launcherDisplay.fillRoundRect(12, y - 5, 216, 34, 4,
                                    ST77XX_BLUE);
    }
    launcherDisplay.setTextColor(ST77XX_WHITE);
    launcherDisplay.setTextSize(1);
    launcherDisplay.setCursor(20, y + 5);
    launcherDisplay.print(labels[i]);
    launcherDisplay.setCursor(135, y + 5);
    launcherDisplay.print(values[i]);
  }
  launcherDisplay.setTextColor(ST77XX_CYAN);
  launcherDisplay.setCursor(8, 270);
  launcherDisplay.print(arcadeText("GORA/DOL: WYBOR", "UP/DOWN: SELECT"));
  launcherDisplay.setCursor(8, 287);
  launcherDisplay.print(arcadeText("LEWO/PRAWO/A: ZMIEN", "LEFT/RIGHT/A: CHANGE"));
  launcherDisplay.setCursor(8, 304);
  launcherDisplay.print(arcadeText("B: POWROT", "B: BACK"));
  syncDisplayInversion();
}

void openSettings(bool fromGameMenu) {
  settingsFromGameMenu = fromGameMenu;
  settingsSelection = 0;
  drawSettings();
}

void closeSettings() {
  if (settingsFromGameMenu) drawGameMenu();
  else drawMainMenu();
}

void changeSelectedSetting(bool increase) {
  switch (settingsSelection) {
    case 0:
      toggleMenuSounds();
      break;
    case 1:
      soundVolume = increase
          ? (soundVolume >= 4 ? 1 : soundVolume + 1)
          : (soundVolume <= 1 ? 4 : soundVolume - 1);
      arcadeSetVolume(soundVolume);
      arcadeStoreUInt("volume", soundVolume);
      break;
    case 2:
      polishLanguage = !polishLanguage;
      arcadeStoreUInt("language", polishLanguage ? 0 : 1);
      break;
    default:
      return;
  }
  drawSettings();
  playMenuSound(900, 25);
}

void drawPlaceholder(const char *title, const char *message) {
  mode = AppMode::Placeholder;
  launcherDisplay.fillScreen(ST77XX_BLACK);
  launcherDisplay.setTextColor(ST77XX_CYAN);
  launcherDisplay.setTextSize(2);
  launcherDisplay.setCursor(12, 20);
  launcherDisplay.println(title);
  launcherDisplay.setTextSize(1);
  launcherDisplay.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  launcherDisplay.setCursor(12, 80);
  launcherDisplay.println(message);
  launcherDisplay.setCursor(12, 290);
  launcherDisplay.println(arcadeText("JOY / Z: POWROT", "JOY / Z: BACK"));
  syncDisplayInversion();
}

void showMissingWad() {
  drawPlaceholder("DOOM", filesystemReady
      ? arcadeText("Brak poprawnego /doom1.wad w LittleFS.",
                   "No valid /doom1.wad found in LittleFS.")
      : arcadeText("Nie udalo sie zamontowac LittleFS.",
                   "Could not mount LittleFS."));
}

bool pressed(int pin) {
  return digitalRead(pin) == LOW;
}

bool pressedEdge(int pin, bool &wasPressed) {
  const bool isPressed = pressed(pin);
  const bool edge = isPressed && !wasPressed;
  wasPressed = isPressed;
  return edge;
}

void blankSleepScreen() {
  launcherDisplay.fillScreen(ST77XX_BLACK);
}

void startSelectedGame() {
  switch (gameSelection) {
    case 0: {
      arcadeStopSound();
      if (!filesystemReady) {
        showMissingWad();
        break;
      }
      File wad = LittleFS.open("/doom1.wad", FILE_READ);
      if (!wad) {
        showMissingWad();
        break;
      }
      char header[4] = {};
      const size_t bytesRead = wad.read(reinterpret_cast<uint8_t *>(header), sizeof(header));
      const size_t wadSize = wad.size();
      wad.close();
      if (bytesRead != sizeof(header) || memcmp(header, "IWAD", sizeof(header)) != 0 || wadSize < 12) {
        showMissingWad();
        break;
      }
      playMenuSound(1000, 45);
      launcherDisplay.init(240, 320);
      launcherDisplay.setRotation(0);
      launcherDisplay.invertDisplay(displayInverted);
      launcherDisplay.fillScreen(ST77XX_BLACK);
      if (!doomCreated) {
        doomgeneric_Create(sizeof(doomArgs) / sizeof(doomArgs[0]), doomArgs);
        doomCreated = true;
      }
      mode = AppMode::Doom;
      break;
    }
    case 1:
      mode = AppMode::Bagman;
      bagman::begin();
      break;
    case 2:
      mode = AppMode::Snake;
      snake::begin();
      break;
    case 3:
      mode = AppMode::Tetris;
      tetris::begin();
      break;
    case 4:
      mode = AppMode::Racer;
      yellowracer::begin();
      break;
    case 5:
      mode = AppMode::Arkanoid;
      arkanoid::begin();
      break;
    default:
      break;
  }
}

void returnToMainMenu() {
  const bool leavingDoom = mode == AppMode::Doom;
  if (leavingDoom) {
    Serial.println("Leaving Doom: stopping audio.");
    arcadeDoomAudioStop();
  }
  gameExitRequested = false;
  for (auto &binding : bindings) binding.wasPressed = false;
  eventRead = eventWrite = 0;
  SPI.begin(pins::tft_clk, pins::tft_miso, pins::tft_mosi, pins::tft_cs);
  launcherDisplay.init(240, 320);
  launcherDisplay.setRotation(0);
  launcherDisplay.invertDisplay(displayInverted);
  previousUpPressed = pressed(pins::joy_up);
  previousDownPressed = pressed(pins::joy_down);
  previousSelectPressed = pressed(pins::joy_button);
  previousInvertPressed = pressed(pins::button_p3);
  previousBackPressed = pressed(pins::button_p4);
  drawMainMenu();
  if (leavingDoom) Serial.println("Doom exit: main menu restored.");
}

void showFatal(const char *message) {
  Serial.printf("Doom startup error: %s\n", message);
  launcherDisplay.fillScreen(ST77XX_BLACK);
  launcherDisplay.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  launcherDisplay.setTextSize(2);
  launcherDisplay.setCursor(8, 12);
  launcherDisplay.println("DOOM STARTUP ERROR");
  launcherDisplay.setTextSize(1);
  launcherDisplay.setCursor(8, 52);
  launcherDisplay.setTextWrap(true);
  launcherDisplay.println(message);
  while (true) {
    delay(1000);
  }
}

bool enqueueKeyEvent(bool pressed, unsigned char key) {
  const size_t next = (eventWrite + 1) % (sizeof(keyEvents) / sizeof(keyEvents[0]));
  if (next == eventRead) {
    return false;
  }
  keyEvents[eventWrite] = {pressed ? 1 : 0, key};
  eventWrite = next;
  return true;
}

void scanButtons() {
  for (auto &binding : bindings) {
    const bool pressed = digitalRead(binding.pin) == LOW;
    if (pressed != binding.wasPressed) {
      if (enqueueKeyEvent(pressed, binding.key)) {
        binding.wasPressed = pressed;
      }
    }
  }
}

}  // namespace

bool arcadeDisplayInverted() {
  return displayInverted;
}

Adafruit_ST7789& arcadeSharedDisplay() {
  return launcherDisplay;
}

void arcadeToggleDisplayInversion() {
  displayInverted = !displayInverted;
}

void arcadeRequestGameExit() {
  gameExitRequested = true;
}

bool arcadeSoundEnabled() {
  return soundsEnabled;
}

void arcadeSetSoundEnabled(bool enabled) {
  soundsEnabled = enabled;
  if (!enabled) arcadeStopSound();
}

void arcadeSetVolume(uint8_t level) {
  if (level < 1) level = 1;
  if (level > 4) level = 4;
  soundVolume = level;
}

uint8_t arcadeVolume() {
  return soundVolume;
}

bool arcadeIsPolish() {
  return polishLanguage;
}

const char* arcadeText(const char* polish, const char* english) {
  return polishLanguage ? polish : english;
}

void arcadePlaySound(uint16_t frequency, uint16_t durationMs) {
  if (!soundsEnabled || !audioReady || frequency == 0 || durationMs == 0) return;
  if (frequency < 180) frequency = 180;
  if (frequency > 3000) frequency = 3000;
  if (durationMs > 150) durationMs = 150;
  ledcAttachPin(pins::audio, audioChannel);
  ledcWriteTone(audioChannel, frequency);
  constexpr uint8_t volumeDuty[] = {0, 32, 64, 96, 128};
  ledcWrite(audioChannel, volumeDuty[soundVolume]);
  soundEndsAtMs = millis() + durationMs;
}

void arcadeStopSound() {
  if (!audioReady) return;
  ledcWrite(audioChannel, 0);
  ledcDetachPin(pins::audio);
  soundEndsAtMs = 0;
}

void arcadeSoundService() {
  if (audioReady && soundEndsAtMs != 0 &&
      static_cast<int32_t>(millis() - soundEndsAtMs) >= 0) {
    arcadeStopSound();
  }
}

extern "C" void *DG_AllocFramebuffer(size_t size) {
  return heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void DG_Init() {
  launcherDisplay.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  launcherDisplay.setTextSize(2);
  launcherDisplay.setCursor(8, 12);
  launcherDisplay.println("Starting Doom...");

  if (DG_ScreenBuffer == nullptr) {
    showFatal("Could not allocate the Doom framebuffer. Check that PSRAM is enabled.");
  }
  if (!DG_InitStateTable()) {
    showFatal("Could not allocate the Doom state table in PSRAM.");
  }

  renderBuffer = static_cast<uint16_t *>(
      heap_caps_malloc(gameImageWidth * gameImageHeight * sizeof(uint16_t),
                       MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (renderBuffer == nullptr) {
    renderBuffer = static_cast<uint16_t *>(
        heap_caps_malloc(gameImageWidth * gameImageHeight * sizeof(uint16_t), MALLOC_CAP_8BIT));
  }
  if (renderBuffer == nullptr) {
    showFatal("Could not allocate the RGB565 display buffer.");
  }

  for (auto &binding : bindings) {
    pinMode(binding.pin, INPUT_PULLUP);
    binding.wasPressed = digitalRead(binding.pin) == LOW;
  }
}

void DG_DrawFrame() {
  for (int y = 0; y < gameImageHeight; ++y) {
    const int sourceY = y * DOOMGENERIC_RESY / gameImageHeight;
    for (int x = 0; x < gameImageWidth; ++x) {
      const int sourceX = x * DOOMGENERIC_RESX / gameImageWidth;
      const uint32_t color =
          static_cast<uint32_t>(DG_ScreenBuffer[sourceY * DOOMGENERIC_RESX + sourceX]);
      const uint16_t rgb565 = static_cast<uint16_t>(((color >> 8) & 0xF800) |
                                                    ((color >> 5) & 0x07E0) |
                                                    ((color >> 3) & 0x001F));
      renderBuffer[y * gameImageWidth + x] = rgb565;
    }
  }
  launcherDisplay.fillRect(0, 0, displayWidth, gameImageYOffset, ST77XX_BLACK);
  launcherDisplay.fillRect(0, gameImageYOffset + gameImageHeight, displayWidth,
                           displayHeight - gameImageYOffset - gameImageHeight,
                           ST77XX_BLACK);
  launcherDisplay.startWrite();
  launcherDisplay.setAddrWindow(0, gameImageYOffset, gameImageWidth, gameImageHeight);
  launcherDisplay.writePixels(renderBuffer, gameImageWidth * gameImageHeight);
  launcherDisplay.endWrite();
}

void DG_SleepMs(uint32_t ms) {
  delay(ms);
}

uint32_t DG_GetTicksMs() {
  return millis();
}

int DG_GetKey(int *pressed, unsigned char *key) {
  scanButtons();
  if (eventRead == eventWrite) {
    return 0;
  }
  const KeyEvent event = keyEvents[eventRead];
  eventRead = (eventRead + 1) % (sizeof(keyEvents) / sizeof(keyEvents[0]));
  *pressed = event.pressed ? 1 : 0;
  *key = event.key;
  return 1;
}

void DG_SetWindowTitle(const char *) {}

void setup() {
  Serial.begin(115200);
  delay(100);

  const int inputPins[] = {
      pins::joy_up, pins::joy_down, pins::joy_left, pins::joy_right, pins::joy_button,
      pins::button_p1, pins::button_p2, pins::button_p3, pins::button_p4,
  };
  for (const int pin : inputPins) pinMode(pin, INPUT_PULLUP);
  previousP2Pressed = pressed(pins::button_p2);
  pinMode(pins::audio, OUTPUT);
  ledcSetup(audioChannel, 1000, 8);
  ledcAttachPin(pins::audio, audioChannel);
  audioReady = true;
  randomSeed(analogRead(0));

  SPI.begin(pins::tft_clk, pins::tft_miso, pins::tft_mosi, pins::tft_cs);
  launcherDisplay.init(240, 320);
  launcherDisplay.setRotation(0);
  launcherDisplay.invertDisplay(displayInverted);
  filesystemReady = LittleFS.begin(false, "/littlefs", 10, "littlefs");
  if (!filesystemReady) Serial.println("LittleFS mount failed; Doom will be unavailable.");
  arcadePersistenceBegin();
  menuSoundsEnabled = arcadeLoadUInt("menu_sound", 1) != 0;
  soundVolume = static_cast<uint8_t>(arcadeLoadUInt("volume", 4));
  if (soundVolume < 1 || soundVolume > 4) soundVolume = 4;
  polishLanguage = arcadeLoadUInt("language", 0) == 0;
  arcadeSetVolume(soundVolume);
  arcadeSetSoundEnabled(menuSoundsEnabled);
  tamagotchi::initialize();
  drawMainMenu();
}

void loop() {
  tamagotchi::service();
  arcadeSoundService();
  updateCoinMarker();
  switch (mode) {
    case AppMode::MainMenu:
      if (pressedEdge(pins::button_p2, previousP2Pressed)) {
        openSettings(false);
      } else if (pressedEdge(pins::joy_down, previousDownPressed)) {
        menuSelection = (menuSelection + 1) % 5;
        drawMainMenu();
        playMenuSound(760, 25);
      } else if (pressedEdge(pins::joy_up, previousUpPressed)) {
        menuSelection = (menuSelection + 4) % 5;
        drawMainMenu();
        playMenuSound(760, 25);
      } else if (pressedEdge(pins::button_p3, previousInvertPressed)) {
        arcadeToggleDisplayInversion();
        syncDisplayInversion();
        playMenuSound(900, 25);
      } else if (pressedEdge(pins::joy_button, previousSelectPressed)) {
        playMenuSound(1100, 45);
        if (menuSelection == 0) drawGameMenu();
        else if (menuSelection == 1) {
          mode = AppMode::Tamagotchi;
          tamagotchi::begin();
        }
        else if (menuSelection == 2) {
          mode = AppMode::Pomodoro;
          pomodoro::begin();
        } else if (menuSelection == 3) {
          mode = AppMode::Screensaver;
          screensaver::begin();
        } else if (menuSelection == 4) {
          mode = AppMode::Sleeping;
          blankSleepScreen();
        }
      }
      break;

    case AppMode::GameMenu:
      if (pressedEdge(pins::button_p2, previousP2Pressed)) {
        openSettings(true);
      } else if (pressedEdge(pins::joy_down, previousDownPressed)) {
        gameSelection = (gameSelection + 1) %
                        6;
        drawGameMenu();
        playMenuSound(760, 25);
      } else if (pressedEdge(pins::joy_up, previousUpPressed)) {
        gameSelection = (gameSelection +
                         5) % 6;
        drawGameMenu();
        playMenuSound(760, 25);
      } else if (pressedEdge(pins::button_p4, previousBackPressed)) {
        playMenuSound(650, 40);
        drawMainMenu();
      } else if (pressedEdge(pins::button_p3, previousInvertPressed)) {
        arcadeToggleDisplayInversion();
        syncDisplayInversion();
        playMenuSound(900, 25);
      } else if (pressedEdge(pins::joy_button, previousSelectPressed)) {
        playMenuSound(1100, 45);
        startSelectedGame();
      }
      break;

    case AppMode::Settings:
      if (pressedEdge(pins::button_p2, previousP2Pressed) ||
          pressedEdge(pins::button_p4, previousBackPressed)) {
        closeSettings();
      } else if (pressedEdge(pins::joy_down, previousDownPressed)) {
        settingsSelection = (settingsSelection + 1) % 4;
        drawSettings();
      } else if (pressedEdge(pins::joy_up, previousUpPressed)) {
        settingsSelection = (settingsSelection + 3) % 4;
        drawSettings();
      } else {
        const bool left = pressedEdge(pins::joy_left, previousLeftPressed);
        const bool right = pressedEdge(pins::joy_right, previousRightPressed);
        const bool joySelect =
            pressedEdge(pins::joy_button, previousSelectPressed);
        const bool buttonSelect =
            pressedEdge(pins::button_p1, previousP1Pressed);
        const bool select = joySelect || buttonSelect;
        if (left || right || select) changeSelectedSetting(!left);
      }
      break;
    case AppMode::Placeholder:
      if (pressedEdge(pins::joy_button, previousSelectPressed) ||
          pressedEdge(pins::button_p4, previousBackPressed)) {
        playMenuSound(650, 40);
        drawMainMenu();
      }
      break;

    case AppMode::Sleeping:
      {
        bool wake = false;
        wake |= pressedEdge(pins::joy_up, previousUpPressed);
        wake |= pressedEdge(pins::joy_down, previousDownPressed);
        wake |= pressedEdge(pins::joy_left, previousLeftPressed);
        wake |= pressedEdge(pins::joy_right, previousRightPressed);
        wake |= pressedEdge(pins::joy_button, previousSelectPressed);
        wake |= pressedEdge(pins::button_p1, previousP1Pressed);
        wake |= pressedEdge(pins::button_p2, previousP2Pressed);
        wake |= pressedEdge(pins::button_p3, previousInvertPressed);
        wake |= pressedEdge(pins::button_p4, previousBackPressed);
        if (wake) {
          drawMainMenu();
        }
      }
      break;

    case AppMode::Doom:
      if (pressed(pins::button_p4)) {
        returnToMainMenu();
        break;
      }
      if (pressedEdge(pins::button_p3, previousInvertPressed)) {
        arcadeToggleDisplayInversion();
        launcherDisplay.invertDisplay(displayInverted);
      }
      doomgeneric_Tick();
      break;

    case AppMode::Bagman:
      bagman::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Snake:
      snake::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Tetris:
      tetris::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Racer:
      yellowracer::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Arkanoid:
      arkanoid::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Pomodoro:
      pomodoro::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Screensaver:
      screensaver::tick();
      if (gameExitRequested) returnToMainMenu();
      break;

    case AppMode::Tamagotchi:
      tamagotchi::tick();
      if (gameExitRequested) returnToMainMenu();
      break;
  }
}
