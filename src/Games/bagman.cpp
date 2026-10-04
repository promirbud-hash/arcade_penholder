#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>
#include <string.h>
#include "arcade_runtime.h"
#include "arcade_persistence.h"

namespace bagman {

// ==========================================
// 1. PINY EKRANU TFT (ST7789)
// ==========================================
#define TFT_CS   14
#define TFT_DC   13
#define TFT_RST  12
#define TFT_MOSI 11 
#define TFT_CLK  10 
#define TFT_MISO -1 

// ==========================================
// 2. PINY JOYSTICKA I PRZYCISKÓW
// ==========================================
#define JOY_UP    17 
#define JOY_DOWN  18 
#define JOY_LEFT  3  
#define JOY_RIGHT 8  
#define JOY_BTN   16 

#define BTN_P1 15 // 1. PAUZA
#define BTN_P2 7  // 2. TURBO (Przytrzymaj)
#define BTN_P3 6  // 3. ODWRÓCENIE KOLORÓW
#define BTN_P4 5  // 4. WYJŚCIE DO MENU

Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

#define SCREEN_W 240
#define SCREEN_H 320

// ==========================================
// PARAMETRY GRY I MAPY
// ==========================================
const int MAP_W = 15;
const int MAP_H = 18;
const int TILE_SIZE = 16;
const int OFFSET_X = 0;
const int OFFSET_Y = 25; 

// Kolory
uint16_t C_BG = tft.color565(30, 30, 30);       // Ciemny asfalt
uint16_t C_WALL = tft.color565(80, 80, 100);    // Mury
uint16_t C_STORE = tft.color565(200, 100, 50);  // Ściany sklepu
uint16_t C_MAN = ST77XX_CYAN;
uint16_t C_WOMAN = ST77XX_MAGENTA;
uint16_t C_SKIN = tft.color565(255, 200, 150);  // Kolor skóry

// Szablony map (1 = Ściana, 0 = Ulica, 2 = Sklep)
const byte MAP_TEMPLATES[3][MAP_H][MAP_W] = {
  { // Mapa 0 (Oryginalna)
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,1,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,0,1,0,1,1,1,1,1,0,1},
    {1,0,1,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,0,1,0,1,1,1,1,1,1,1,0,1,0,1},
    {1,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
    {1,1,1,0,1,0,1,1,1,0,1,0,1,1,1},
    {1,0,0,0,0,0,2,2,2,0,0,0,0,0,1}, 
    {1,0,1,1,1,0,2,0,2,0,1,1,1,0,1}, 
    {1,0,0,0,0,0,2,0,2,0,0,0,0,0,1}, 
    {1,1,1,0,1,0,1,0,1,0,1,0,1,1,1},
    {1,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
    {1,0,1,0,1,0,1,1,1,0,1,0,1,0,1},
    {1,0,1,0,0,0,0,0,0,0,0,0,1,0,1},
    {1,0,1,1,1,1,1,0,1,1,1,1,1,0,1},
    {1,0,0,0,0,0,1,0,1,0,0,0,0,0,1},
    {1,1,1,1,1,0,0,0,0,0,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
  },
  { // Mapa 1 (Szerokie alejki)
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,1,0,1,0,1,1,1,1,0,1},
    {1,0,1,0,0,0,0,1,0,0,0,0,1,0,1},
    {1,0,1,0,1,1,1,1,1,1,1,0,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,0,1,1,1,1,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,2,2,2,0,0,0,0,0,1},
    {1,0,1,1,1,0,2,0,2,0,1,1,1,0,1},
    {1,0,0,0,0,0,2,0,2,0,0,0,0,0,1},
    {1,1,1,0,1,1,1,1,1,1,1,0,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,0,1,1,1,1,1,1,1,0,1,0,1},
    {1,0,1,0,0,0,0,1,0,0,0,0,1,0,1},
    {1,0,1,1,1,1,0,1,0,1,1,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
  },
  { // Mapa 2 (Dwa główne ronda)
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
    {1,0,1,0,1,0,1,1,1,0,1,0,1,0,1},
    {1,0,1,0,0,0,1,0,1,0,0,0,1,0,1},
    {1,0,1,1,1,0,1,1,1,0,1,1,1,0,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,1,1,1,1,0,1,1,1,0,1,1,1,1,1},
    {1,0,0,0,0,0,2,2,2,0,0,0,0,0,1},
    {1,0,1,1,1,0,2,0,2,0,1,1,1,0,1},
    {1,0,0,0,0,0,2,0,2,0,0,0,0,0,1},
    {1,1,1,1,1,0,1,1,1,0,1,1,1,1,1},
    {1,0,0,0,0,0,0,0,0,0,0,0,0,0,1},
    {1,0,1,1,1,0,1,1,1,0,1,1,1,0,1},
    {1,0,1,0,1,0,1,0,1,0,1,0,1,0,1},
    {1,0,1,0,1,0,1,1,1,0,1,0,1,0,1},
    {1,0,0,0,1,0,0,0,0,0,1,0,0,0,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1},
    {1,1,1,1,1,1,1,1,1,1,1,1,1,1,1}
  }
};

byte (*gameMap)[MAP_W] = nullptr;

// ==========================================
// ZMIENNE STANU I SYSTEMU
// ==========================================
enum GameState { MENU, STORY, PLAYING, GAME_OVER };
GameState state = MENU;

int menuSelection = 0;
int highScore = 0;
unsigned long lastBtnPress = 0;

bool isPaused = false;
bool previousInvertButton = false;
bool previousExitButton = false;

// Zmienne Fabuły
int storyY = 320;
unsigned long lastStoryScroll = 0;
constexpr const char *storyLines[] = {
    "DAWNO, DAWNO TEMU",
    "W MIESCIE, CO",
    "NIE ZASYPIA",
    "NAWET PO ZMROKU,",
    "ZMECZONY CZLOWIEK",
    "MARZYL O WIECZORZE",
    "CICHYM JAK SZEPT",
    "I CHLODNYM",
    "PIWIE, CO",
    "CZEKA W DOMU.",
    "LECZ LOS, JAK",
    "PRZEWROTNY WIATR,",
    "ZAGRODZIL MU",
    "DROGE DO WOLNOSCI.",
    "ZZA DRZWI PADL",
    "GLOS NIEZNOSNY:",
    "\"NAJPIERW ZAKUPY!\"",
    "W JEGO DLON",
    "TRAFILA LISTA",
    "DLUGA JAK NOC,",
    "A SKLEP ZAMIENIL",
    "SIE W LABIRYNT",
    "ALEJEK I CIENI.",
    "PO KORYTARZACH",
    "KROCZY STRAZNICZKA,",
    "CZUJNA I SUROWA,",
    "GOTOWA PRZECIAC",
    "KAZDA DROGE.",
    "BAG-MAN NIE",
    "MOZE SIE PODDAC.",
    "ZBIERA PIWNE",
    "BUTELKI, MIJA",
    "PULAPKI I SZUKA",
    "LUKI W MURACH.",
    "KAZDY KROK TO",
    "ISKIERKA NADZIEI,",
    "KAZDY ZAKRET",
    "PRZYBLIZA GO",
    "DO OTWARTEJ BRAMY.",
    "CZY ZDOLA WROCIC",
    "DO DOMU PRZED",
    "OSTATNIM DZWONKIEM?",
    "CZY ZASLUZY",
    "NA CHWILE CISZY,",
    "NA LODOWATE PIWO",
    "I WIECZOR BEZ",
    "KOLEJNEJ LISTY?",
    "O TYM ZDECYDUJE",
    "TWOJ SPRYT.",
};
constexpr const char *englishStoryLines[] = {
    "LONG AGO,",
    "IN A CITY THAT",
    "NEVER SLEEPS",
    "AFTER DUSK,",
    "A TIRED MAN",
    "DREAMED OF AN",
    "EVENING QUIET",
    "AS A WHISPER,",
    "AND COLD BEER",
    "WAITING AT HOME.",
    "BUT FATE, LIKE",
    "A SHIFTING WIND,",
    "BLOCKED THE ROAD",
    "TO HIS FREEDOM.",
    "FROM BEHIND THE DOOR",
    "A VOICE RANG OUT:",
    "\"SHOPPING FIRST!\"",
    "IN HIS HAND",
    "HE FOUND A LIST",
    "LONG AS THE NIGHT,",
    "AND THE STORE",
    "BECAME A MAZE",
    "OF AISLES AND SHADE.",
    "THROUGH THE HALLS",
    "THE WATCHFUL GUARD",
    "PATROLS, ALERT",
    "AND READY TO",
    "CUT OFF HIS PATH.",
    "BAG-MAN WILL",
    "NOT GIVE IN.",
    "HE GATHERS BEER",
    "AND DODGES TRAPS,",
    "SEEKING A GAP",
    "IN THE WALLS.",
    "EACH STEP IS",
    "A SPARK OF HOPE,",
    "EACH TURN",
    "BRINGS HIM CLOSER",
    "TO THE OPEN GATE.",
    "CAN HE GET HOME",
    "BEFORE THE FINAL",
    "BELL RINGS OUT?",
    "WILL HE EARN",
    "A MOMENT OF PEACE,",
    "AN ICE-COLD BEER",
    "AND AN EVENING",
    "FREE OF ONE MORE LIST?",
    "THAT WILL BE DECIDED",
    "BY YOUR SKILL.",
};
constexpr size_t storyLineCount = sizeof(storyLines) / sizeof(storyLines[0]);
static_assert(storyLineCount ==
                  sizeof(englishStoryLines) / sizeof(englishStoryLines[0]),
              "Story translations must have matching line counts.");
const char *storyLine(size_t index) {
  return arcadeIsPolish() ? storyLines[index] : englishStoryLines[index];
}
int16_t previousStoryX[storyLineCount] = {};
int16_t previousStoryY[storyLineCount] = {};
uint8_t previousStorySize[storyLineCount] = {};
bool previousStoryVisible[storyLineCount] = {};

// Postacie
int manX = 1, manY = 1;
int nextDirX = 0, nextDirY = 0;
int dirX = 0, dirY = 0;
int lastFaceX = 1, lastFaceY = 0; 
unsigned long lastManMove = 0;

int womanX = 7, womanY = 8; 
unsigned long lastWomanMove = 0;
const int WOMAN_SPEED = 350; 

int beerX = -1, beerY = -1;
int score = 0;

// ==========================================
// FUNKCJE AUDIO
// ==========================================
void playSound(int frequency, int duration) {
  arcadePlaySound(static_cast<uint16_t>(frequency), static_cast<uint16_t>(duration));
}

void updateHighScore() {
  if (score > highScore) {
    highScore = score;
    arcadeStoreHighScore("bagman_best", static_cast<uint32_t>(highScore));
  }
}

// ==========================================
// FUNKCJE RYSOWANIA
// ==========================================
void drawBeer(int x, int y) {
  int px = x * TILE_SIZE;
  int py = y * TILE_SIZE + OFFSET_Y;
  tft.fillRect(px + 4, py + 4, 8, 10, ST77XX_YELLOW);
  tft.fillRect(px + 3, py + 2, 10, 4, ST77XX_WHITE);
  tft.drawFastVLine(px + 12, py + 5, 6, ST77XX_YELLOW);
}

void restoreTile(int x, int y) {
  int px = x * TILE_SIZE;
  int py = y * TILE_SIZE + OFFSET_Y;
  
  if (gameMap[y][x] == 1) tft.fillRect(px, py, TILE_SIZE, TILE_SIZE, C_WALL);
  else if (gameMap[y][x] == 2) tft.fillRect(px, py, TILE_SIZE, TILE_SIZE, C_STORE);
  else tft.fillRect(px, py, TILE_SIZE, TILE_SIZE, C_BG);
  
  if (x == beerX && y == beerY) drawBeer(beerX, beerY);
}

void drawMan(int x, int y, int dX, int dY) {
  int px = x * TILE_SIZE + TILE_SIZE/2;
  int py = y * TILE_SIZE + OFFSET_Y + TILE_SIZE/2;
  
  if (dX != 0 || dY != 0) { lastFaceX = dX; lastFaceY = dY; }
  
  if (lastFaceX != 0) {
    tft.fillRect(px - 4, py - 6, 8, 12, C_MAN);
    tft.fillCircle(px + (3 * lastFaceX), py - 5, 2, C_SKIN);
    tft.fillCircle(px + (3 * lastFaceX), py + 5, 2, C_SKIN);
  } else {
    tft.fillRect(px - 6, py - 4, 12, 8, C_MAN);
    tft.fillCircle(px - 5, py + (3 * lastFaceY), 2, C_SKIN);
    tft.fillCircle(px + 5, py + (3 * lastFaceY), 2, C_SKIN);
  }
  tft.fillCircle(px, py, 4, C_SKIN);
}

void drawWoman(int x, int y) {
  int px = x * TILE_SIZE + TILE_SIZE/2;
  int py = y * TILE_SIZE + OFFSET_Y + TILE_SIZE/2;
  
  tft.fillCircle(px, py, 6, C_WOMAN);
  tft.fillRect(px - 8, py - 4, 4, 8, ST77XX_WHITE);
  tft.fillRect(px + 4, py - 4, 4, 8, ST77XX_WHITE);
  tft.fillCircle(px, py, 4, tft.color565(200, 150, 50)); 
}

void spawnBeer() {
  bool valid = false;
  while (!valid) {
    beerX = random(1, MAP_W - 1);
    beerY = random(1, MAP_H - 1);
    if (gameMap[beerY][beerX] == 0 && (beerX != manX || beerY != manY)) {
      valid = true;
    }
  }
  drawBeer(beerX, beerY);
}

void loadRandomMap() {
  int mapIndex = random(0, 3);
  for (int y = 0; y < MAP_H; y++) {
    for (int x = 0; x < MAP_W; x++) {
      gameMap[y][x] = MAP_TEMPLATES[mapIndex][y][x];
    }
  }
}

void drawMap() {
  tft.fillScreen(C_BG);
  for (int y = 0; y < MAP_H; y++) {
    for (int x = 0; x < MAP_W; x++) {
      if (gameMap[y][x] == 1) tft.fillRect(x * TILE_SIZE, y * TILE_SIZE + OFFSET_Y, TILE_SIZE, TILE_SIZE, C_WALL);
      else if (gameMap[y][x] == 2) tft.fillRect(x * TILE_SIZE, y * TILE_SIZE + OFFSET_Y, TILE_SIZE, TILE_SIZE, C_STORE);
    }
  }
}

void drawUI() {
  tft.fillRect(0, 0, SCREEN_W, 25, ST77XX_BLACK);
  tft.drawLine(0, 24, SCREEN_W, 24, ST77XX_WHITE);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(5, 8);
  tft.print(String(arcadeText("PIWA: ", "BEER: ")) + score);
  tft.setCursor(140, 8);
  tft.print(String(arcadeText("REKORD: ", "BEST: ")) + highScore);
}

// ==========================================
// EKRAN STARTOWY / MENU
// ==========================================
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(5, 5, 230, 310, C_MAN);
  
  tft.setTextColor(C_STORE);
  tft.setTextSize(3);
  tft.setCursor(45, 40);
  tft.print("BAG-MAN");
  
  tft.setTextSize(2);
  String options[3] = {
      arcadeText("START GRY", "START GAME"),
      arcadeText("FABULA", "STORY"),
      arcadeText("WYJSCIE", "EXIT")};
  
  for (int i = 0; i < 3; i++) {
    int yPos = 120 + (i * 40);
    if (i == menuSelection) {
      tft.fillRect(20, yPos - 5, 200, 35, tft.color565(80, 80, 80));
      tft.setTextColor(ST77XX_WHITE);
    } else {
      tft.setTextColor(C_WALL);
    }
    tft.setCursor(35, yPos);
    tft.print(options[i]);
  }
  
  tft.setTextColor(ST77XX_ORANGE);
  tft.setTextSize(1);
  tft.setCursor(55, 290);
  tft.print(String(arcadeText("REKORD: ", "HIGH SCORE: ")) + highScore);
}

void drawStoryCrawl() {
  constexpr int16_t stars[][2] = {
      {17, 38}, {58, 72}, {111, 31}, {205, 58}, {226, 119},
      {32, 155}, {184, 183}, {83, 212}, {218, 246}, {13, 278},
      {145, 102}, {164, 282}, {52, 112}, {196, 225}, {104, 265},
  };
  for (size_t i = 0; i < storyLineCount; ++i) {
    if (!previousStoryVisible[i]) continue;
    const int16_t width = static_cast<int16_t>(
        strlen(storyLine(i)) * 6 * previousStorySize[i]);
    const int16_t height = static_cast<int16_t>(8 * previousStorySize[i]);
    tft.fillRect(previousStoryX[i] - 1, previousStoryY[i] - 1,
                 width + 2, height + 2, ST77XX_BLACK);
    previousStoryVisible[i] = false;
  }

  for (const auto &star : stars) {
    tft.drawPixel(star[0], star[1], ST77XX_WHITE);
  }

  constexpr float horizon = 58.0f;
  constexpr float crawlHeight = 262.0f;
  constexpr int16_t lineSpacing = 27;
  for (size_t i = 0; i < storyLineCount; ++i) {
    const float virtualY = static_cast<float>(storyY + i * lineSpacing);
    if (virtualY <= horizon || virtualY >= SCREEN_H + 24) continue;

    const float normalized = (virtualY - horizon) / crawlHeight;
    const int16_t y = static_cast<int16_t>(
        horizon + crawlHeight * powf(normalized, 1.18f));
    uint8_t textSize = y > 238 ? 3 : (y > 142 ? 2 : 1);
    const size_t textLength = strlen(storyLine(i));
    const uint8_t maxSize = static_cast<uint8_t>(
        max<size_t>(1, min<size_t>(3, 228 / (6 * textLength))));
    if (textSize > maxSize) textSize = maxSize;

    const int16_t textWidth = static_cast<int16_t>(textLength * 6 * textSize);
    const int16_t x = (SCREEN_W - textWidth) / 2;
    previousStoryX[i] = x;
    previousStoryY[i] = y;
    previousStorySize[i] = textSize;
    previousStoryVisible[i] = true;
    tft.setTextColor(ST77XX_YELLOW);
    tft.setTextSize(textSize);
    tft.setCursor(x, y);
    tft.print(storyLine(i));
  }
}

void initGame() {
  score = 0;
  manX = 1; manY = 1;
  womanX = 7; womanY = 8;
  dirX = 0; dirY = 0; nextDirX = 0; nextDirY = 0;
  isPaused = false;
  
  loadRandomMap();
  drawMap();
  drawUI();
  spawnBeer();
  playSound(1000, 300);
}

void begin() {
  state = MENU;
  menuSelection = 0;
  highScore = static_cast<int>(arcadeLoadUInt("bagman_best", 0));
  previousInvertButton = false;
  previousExitButton = false;
  SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
  
  tft.init(240, 320);
  tft.setRotation(0);
  tft.invertDisplay(arcadeDisplayInverted());

  pinMode(JOY_UP, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_LEFT, INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(JOY_BTN, INPUT_PULLUP);
  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);
  pinMode(BTN_P3, INPUT_PULLUP);
  pinMode(BTN_P4, INPUT_PULLUP);

  if (gameMap == nullptr) {
    gameMap = static_cast<byte (*)[MAP_W]>(
        heap_caps_malloc(MAP_H * sizeof(*gameMap), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (gameMap == nullptr) {
    Serial.println("Bag-Man startup error: unable to allocate game map in PSRAM.");
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(20, 140);
    tft.println(arcadeText("BRAK PAMIECI", "OUT OF MEMORY"));
    return;
  }
  
  randomSeed(analogRead(0));

  drawMenu();
  playSound(1200, 200);
}

void tick() {
  unsigned long currentMillis = millis();
  const bool invertPressed = digitalRead(BTN_P3) == LOW;
  if (invertPressed && !previousInvertButton) {
    arcadeToggleDisplayInversion();
    tft.invertDisplay(arcadeDisplayInverted());
  }
  previousInvertButton = invertPressed;
  const bool exitPressed = digitalRead(BTN_P4) == LOW;
  const bool exitPressedEdge = exitPressed && !previousExitButton;
  previousExitButton = exitPressed;
  if (exitPressedEdge) {
    if (state == PLAYING) updateHighScore();
    arcadeRequestGameExit();
    return;
  }
  if (gameMap == nullptr) return;

  // ==================================================
  // OBSŁUGA STANU: MENU
  // ==================================================
  if (state == MENU) {
    if (currentMillis - lastBtnPress > 200) {
      if (digitalRead(JOY_DOWN) == LOW) {
        menuSelection = (menuSelection + 1) % 3;
        drawMenu();
        playSound(800, 20);
        lastBtnPress = currentMillis;
      }
      else if (digitalRead(JOY_UP) == LOW) {
        menuSelection = (menuSelection - 1 + 3) % 3;
        drawMenu();
        playSound(800, 20);
        lastBtnPress = currentMillis;
      }
      else if (digitalRead(JOY_BTN) == LOW) {
        playSound(1500, 50);
        lastBtnPress = currentMillis;
        
        if (menuSelection == 0) {
          state = PLAYING;
          initGame();
        } 
        else if (menuSelection == 1) {
          state = STORY;
          storyY = 320;
          for (size_t i = 0; i < storyLineCount; ++i) {
            previousStoryVisible[i] = false;
          }
          lastStoryScroll = currentMillis;
          tft.fillScreen(ST77XX_BLACK);
        } else if (menuSelection == 2) {
          arcadeRequestGameExit();
          return;
        }
      }
    }
  }
  // ==================================================
  // OBSŁUGA STANU: FABUŁA (Scrolowany tekst)
  // ==================================================
  else if (state == STORY) {
    if (currentMillis - lastStoryScroll > 60) {
      lastStoryScroll = currentMillis;
      storyY -= 2;
      drawStoryCrawl();

      if (storyY + storyLineCount * 27 < 58) {
        storyY = SCREEN_H;
      }
    } else {
      delay(1);
    }

    // Wyjście z fabuły dowolnym przyciskiem
    if (digitalRead(JOY_BTN) == LOW || digitalRead(BTN_P1) == LOW || digitalRead(BTN_P4) == LOW) {
      if (currentMillis - lastBtnPress > 300) {
        state = MENU;
        drawMenu();
        lastBtnPress = currentMillis;
      }
    }
  }
  // ==================================================
  // OBSŁUGA STANU: GRA
  // ==================================================
  else if (state == PLAYING) {

    // --- KLAWISZE FUNKCYJNE ---
    if (currentMillis - lastBtnPress > 300) {
      if (digitalRead(BTN_P1) == LOW) {
        isPaused = !isPaused;
        playSound(800, 100);
        lastBtnPress = currentMillis;
        
        if (isPaused) {
          tft.fillRect(70, 140, 100, 40, ST77XX_BLACK);
          tft.drawRect(70, 140, 100, 40, ST77XX_YELLOW);
          tft.setTextColor(ST77XX_YELLOW);
          tft.setTextSize(2);
          tft.setCursor(85, 152);
          tft.print(arcadeText("PAUZA", "PAUSED"));
        } else {
          drawMap(); drawUI(); drawBeer(beerX, beerY); drawWoman(womanX, womanY); drawMan(manX, manY, dirX, dirY);
        }
      }
      
      if (digitalRead(BTN_P4) == LOW) {
        arcadeRequestGameExit();
        return;
      }
    }

    if (isPaused) return;

    if (digitalRead(JOY_UP) == LOW)    { nextDirX = 0; nextDirY = -1; }
    if (digitalRead(JOY_DOWN) == LOW)  { nextDirX = 0; nextDirY = 1; }
    if (digitalRead(JOY_LEFT) == LOW)  { nextDirX = -1; nextDirY = 0; }
    if (digitalRead(JOY_RIGHT) == LOW) { nextDirX = 1; nextDirY = 0; }

    int manSpeed = (digitalRead(BTN_P2) == LOW) ? 60 : 120; // Turbo

    // --- RUCH GRACZA ---
    if (currentMillis - lastManMove > manSpeed) {
      lastManMove = currentMillis;

      if (gameMap[manY + nextDirY][manX + nextDirX] != 1) {
        dirX = nextDirX; dirY = nextDirY;
      }

      int targetX = manX + dirX;
      int targetY = manY + dirY;
      
      if (targetX < 0) targetX = MAP_W - 1;
      if (targetX >= MAP_W) targetX = 0;

      if (targetY >= 0 && targetY < MAP_H && gameMap[targetY][targetX] != 1) {
        restoreTile(manX, manY); 
        manX = targetX;
        manY = targetY;
        
        if (manX == beerX && manY == beerY) {
          score++;
          drawUI();
          playSound(2000, 50);
          spawnBeer();
        }
      }

      drawMan(manX, manY, dirX, dirY);
    }

    // --- RUCH KOBIETY (AI) ---
    if (currentMillis - lastWomanMove > WOMAN_SPEED) {
      lastWomanMove = currentMillis;
      restoreTile(womanX, womanY); 

      if (abs(manX - womanX) > abs(manY - womanY)) {
        if (womanX < manX) womanX++; else if (womanX > manX) womanX--;
      } else {
        if (womanY < manY) womanY++; else if (womanY > manY) womanY--;
      }
      
      drawWoman(womanX, womanY);
    }

    // --- ZŁAPANIE (KOLIZJA) ---
    if (manX == womanX && manY == womanY) {
      playSound(150, 800);
      updateHighScore();
      state = GAME_OVER;
    }
  }
  // ==================================================
  // OBSŁUGA STANU: GAME OVER
  // ==================================================
  else if (state == GAME_OVER) {
    tft.fillRect(40, 120, 160, 80, ST77XX_BLACK);
    tft.drawRect(40, 120, 160, 80, ST77XX_RED);
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(3);
    tft.setCursor(65, 135);
    tft.print(arcadeText("ZLAPANY", "CAUGHT"));
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(70, 175);
    tft.print(arcadeText("JOY: MENU", "JOY: MENU"));

    if (digitalRead(JOY_BTN) == LOW && currentMillis - lastBtnPress > 500) {
      state = MENU;
      drawMenu();
      lastBtnPress = currentMillis;
    }
  }
}

}  // namespace bagman