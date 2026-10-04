#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <esp_heap_caps.h>
#include "arcade_runtime.h"
#include "arcade_persistence.h"

namespace snake {

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
// 2. PINY STEROWANIA
// ==========================================
#define JOY_UP    17 
#define JOY_DOWN  18 
#define JOY_LEFT  3  
#define JOY_RIGHT 8  
#define JOY_BTN   16 

// KLAWISZE AKCJI
#define BTN_P1 15 // Wyjście do Menu
#define BTN_P2 7  // Pauza
#define BTN_P3 6  // Odwrócenie kolorów (Inwersja)
#define BTN_P4 5

// ==========================================
// 3. USTAWIENIA EKRANU I GRAFIKI
// ==========================================
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

#define SCREEN_W 240
#define SCREEN_H 320

uint16_t C_BG = tft.color565(30, 40, 30);          // Tło planszy (ciemna zieleń)
uint16_t C_WALL = tft.color565(150, 150, 150);     // Ściany ramki
uint16_t C_SNAKE_BODY = tft.color565(50, 205, 50); // Tułów (jasny zielony)
uint16_t C_SNAKE_STRIPE = tft.color565(0, 100, 0); // Paski na ciele (ciemny zielony)
uint16_t C_SNAKE_HEAD = tft.color565(0, 255, 0);   // Głowa (żarówiasty zielony)
uint16_t C_FOOD = tft.color565(255, 0, 0);         // Czerwone jabłko
uint16_t C_SPECIAL = tft.color565(255, 215, 0);    // Złote jabłko (specjalne)

// ==========================================
// 4. ZMIENNE GRY
// ==========================================
enum GameState { MENU, PLAYING, GAME_OVER };
GameState state = MENU;

// Zmienne Menu i Systemowe
int menuSelection = 0;
int currentLevel = 1;
int highScore = 0;
unsigned long lastBtnPress = 0;

bool isPaused = false;
bool previousInvertButton = false;
bool previousExitButton = false;

// Zmienne Silnika Węża
const int GRID = 10;
const int MAX_LEN = 300;
int16_t *sX = nullptr;
int16_t *sY = nullptr;
int sLen = 5;

int dir = 1;      
int moveQueue[2] = {-1, -1}; 
int lastJoyDir = -1;

int score = 0;
int applesEaten = 0;
unsigned long lastMoveTime = 0;
int gameSpeed = 150; 

void updateHighScore() {
  if (score > highScore) {
    highScore = score;
    arcadeStoreHighScore("snake_best", static_cast<uint32_t>(highScore));
  }
}

// Owoce
int foodX, foodY;
int specialX = -1, specialY = -1;
bool specialActive = false;
unsigned long specialSpawnTime = 0;

// ==========================================
// FUNKCJE AUDIO
// ==========================================
void playSound(int frequency, int duration) {
  arcadePlaySound(static_cast<uint16_t>(frequency), static_cast<uint16_t>(duration));
}

// ==========================================
// FUNKCJE RYSOWANIA
// ==========================================
void drawHead(int x, int y, int d) {
  tft.fillRect(x, y, GRID, GRID, C_BG);
  if (d == 0) { 
    tft.fillRect(x+3, y+6, 4, 4, C_SNAKE_BODY); 
    tft.fillRoundRect(x+1, y+1, 8, 6, 2, C_SNAKE_HEAD); 
    tft.drawPixel(x+2, y+3, ST77XX_RED); tft.drawPixel(x+7, y+3, ST77XX_RED); 
  } else if (d == 1) { 
    tft.fillRect(x, y+3, 4, 4, C_SNAKE_BODY); 
    tft.fillRoundRect(x+3, y+1, 6, 8, 2, C_SNAKE_HEAD); 
    tft.drawPixel(x+6, y+2, ST77XX_RED); tft.drawPixel(x+6, y+7, ST77XX_RED); 
  } else if (d == 2) { 
    tft.fillRect(x+3, y, 4, 4, C_SNAKE_BODY); 
    tft.fillRoundRect(x+1, y+3, 8, 6, 2, C_SNAKE_HEAD); 
    tft.drawPixel(x+2, y+6, ST77XX_RED); tft.drawPixel(x+7, y+6, ST77XX_RED); 
  } else if (d == 3) { 
    tft.fillRect(x+6, y+3, 4, 4, C_SNAKE_BODY); 
    tft.fillRoundRect(x+1, y+1, 6, 8, 2, C_SNAKE_HEAD); 
    tft.drawPixel(x+3, y+2, ST77XX_RED); tft.drawPixel(x+3, y+7, ST77XX_RED); 
  }
}

void drawBody(int x, int y, int prevX, int prevY) {
  tft.fillRect(x+1, y+1, 8, 8, C_SNAKE_BODY);
  if (prevX != x) { 
    tft.drawFastVLine(x+3, y+1, 8, C_SNAKE_STRIPE);
    tft.drawFastVLine(x+6, y+1, 8, C_SNAKE_STRIPE);
  } else { 
    tft.drawFastHLine(x+1, y+3, 8, C_SNAKE_STRIPE);
    tft.drawFastHLine(x+1, y+6, 8, C_SNAKE_STRIPE);
  }
}

void drawTail(int x, int y, int nextX, int nextY) {
  tft.fillRect(x, y, GRID, GRID, C_BG);
  if (nextX > x) tft.fillTriangle(x+9, y+1, x+9, y+9, x+1, y+5, C_SNAKE_BODY);
  else if (nextX < x) tft.fillTriangle(x+1, y+1, x+1, y+9, x+9, y+5, C_SNAKE_BODY);
  else if (nextY > y) tft.fillTriangle(x+1, y+9, x+9, y+9, x+5, y+1, C_SNAKE_BODY);
  else tft.fillTriangle(x+1, y+1, x+9, y+1, x+5, y+9, C_SNAKE_BODY);
}

void redrawGameScreen() {
  // Funkcja czyszcząca ekran z napisu PAUZA i rysująca wszystko od nowa
  tft.fillScreen(C_BG);
  tft.drawRect(5, 35, 230, 280, C_WALL);
  tft.drawRect(6, 36, 228, 278, C_WALL);
  
  tft.setTextColor(ST77XX_WHITE, C_BG);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print(String(arcadeText("WYNIK: ", "SCORE: ")) + score);

  // Rysowanie owoców
  tft.fillCircle(foodX + 5, foodY + 5, 4, C_FOOD);
  if (specialActive) {
    tft.fillCircle(specialX + 5, specialY + 5, 4, C_SPECIAL);
    tft.drawPixel(specialX + 5, specialY + 1, ST77XX_GREEN);
  }

  // Rysowanie węża
  drawHead(sX[0], sY[0], dir);
  for(int i = 1; i < sLen - 1; i++) drawBody(sX[i], sY[i], sX[i-1], sY[i-1]);
  drawTail(sX[sLen-1], sY[sLen-1], sX[sLen-2], sY[sLen-2]);
}

// ==========================================
// FUNKCJE LOGIKI GRY
// ==========================================
void spawnFood() {
  bool valid = false;
  while (!valid) {
    valid = true;
    foodX = random(1, 23) * GRID; 
    foodY = random(4, 31) * GRID; 
    for (int i = 0; i < sLen; i++) {
      if ((sX[i] == foodX && sY[i] == foodY) || (specialActive && specialX == foodX && specialY == foodY)) {
        valid = false;
      }
    }
  }
  tft.fillCircle(foodX + 5, foodY + 5, 4, C_FOOD);
}

void spawnSpecialFood() {
  bool valid = false;
  while (!valid) {
    valid = true;
    specialX = random(1, 23) * GRID; 
    specialY = random(4, 31) * GRID; 
    for (int i = 0; i < sLen; i++) {
      if ((sX[i] == specialX && sY[i] == specialY) || (foodX == specialX && foodY == specialY)) {
        valid = false;
      }
    }
  }
  specialActive = true;
  specialSpawnTime = millis();
  tft.fillCircle(specialX + 5, specialY + 5, 4, C_SPECIAL);
  tft.drawPixel(specialX + 5, specialY + 1, ST77XX_GREEN); 
  playSound(2500, 150);
}

void pushMove(int newDir) {
  int lastMove = dir;
  if (moveQueue[0] != -1) lastMove = moveQueue[0];
  if (moveQueue[1] != -1) lastMove = moveQueue[1];

  if (newDir == lastMove) return;
  if (newDir == 0 && lastMove == 2) return;
  if (newDir == 1 && lastMove == 3) return;
  if (newDir == 2 && lastMove == 0) return;
  if (newDir == 3 && lastMove == 1) return;

  if (moveQueue[0] == -1) moveQueue[0] = newDir;
  else if (moveQueue[1] == -1) moveQueue[1] = newDir;
}

void initGame() {
  score = 0;
  applesEaten = 0;
  sLen = 5;
  dir = 1;
  moveQueue[0] = -1; moveQueue[1] = -1;
  isPaused = false;
  
  gameSpeed = 160 - (currentLevel * 10); 
  specialActive = false;

  for(int i = 0; i < sLen; i++) {
    sX[i] = 120 - (i * GRID);
    sY[i] = 160;
  }
  
  redrawGameScreen();
  spawnFood();
}

// ==========================================
// SYSTEM MENU
// ==========================================
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(5, 5, 230, 310, C_SNAKE_HEAD);
  
  tft.setTextColor(ST77XX_YELLOW);
  tft.setTextSize(3);
  tft.setCursor(45, 30);
  tft.print("SNAKE");
  tft.setCursor(35, 65);
  tft.print("DELUXE");

  tft.setTextSize(2);
  String options[2] = {
      arcadeText("NOWA GRA", "NEW GAME"),
      String(arcadeText("POZIOM: ", "LEVEL: ")) + currentLevel};
  
  for (int i = 0; i < 2; i++) {
    int yPos = 140 + (i * 40);
    if (i == menuSelection) {
      // NAPRAWIONY BŁĄD Z KOLOREM: Używamy ręcznie wygenerowanego szarego
      tft.fillRect(20, yPos - 5, 200, 30, tft.color565(80, 80, 80));
      tft.setTextColor(ST77XX_WHITE);
    } else {
      tft.setTextColor(C_SNAKE_BODY);
    }
    tft.setCursor(35, yPos);
    tft.print(options[i]);
  }
  
  tft.setTextColor(ST77XX_ORANGE);
  tft.setTextSize(1);
  tft.setCursor(55, 280);
  tft.print(arcadeText("REKORD: ", "HIGH SCORE: "));
  tft.print(highScore);
}

void begin() {
  state = MENU;
  menuSelection = 0;
  highScore = static_cast<int>(arcadeLoadUInt("snake_best", 0));
  previousInvertButton = false;
  previousExitButton = false;
  SPI.begin(TFT_CLK, TFT_MISO, TFT_MOSI, TFT_CS);
  tft.init(240, 320); 
  tft.setRotation(0); 
  tft.invertDisplay(arcadeDisplayInverted());

  if (sX == nullptr) {
    sX = static_cast<int16_t *>(heap_caps_malloc(MAX_LEN * sizeof(*sX),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (sY == nullptr) {
    sY = static_cast<int16_t *>(heap_caps_malloc(MAX_LEN * sizeof(*sY),
                                                 MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  }
  if (sX == nullptr || sY == nullptr) {
    Serial.println("Snake startup error: unable to allocate snake body in PSRAM.");
    tft.fillScreen(ST77XX_BLACK);
    tft.setTextColor(ST77XX_RED);
    tft.setCursor(12, 120);
    tft.println(arcadeText("BRAK PAMIECI", "OUT OF MEMORY"));
    return;
  }

  pinMode(JOY_UP, INPUT_PULLUP);
  pinMode(JOY_DOWN, INPUT_PULLUP);
  pinMode(JOY_LEFT, INPUT_PULLUP);
  pinMode(JOY_RIGHT, INPUT_PULLUP);
  pinMode(JOY_BTN, INPUT_PULLUP);
  
  pinMode(BTN_P1, INPUT_PULLUP);
  pinMode(BTN_P2, INPUT_PULLUP);
  pinMode(BTN_P3, INPUT_PULLUP);
  pinMode(BTN_P4, INPUT_PULLUP);
  
  drawMenu();
  playSound(1000, 200);
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
  if (sX == nullptr || sY == nullptr) return;

  // ==================================================
  // OBSŁUGA STANU: MENU
  // ==================================================
  if (state == MENU) {
    if (currentMillis - lastBtnPress > 200) {
      if (digitalRead(JOY_DOWN) == LOW) {
        menuSelection = (menuSelection + 1) % 2;
        drawMenu();
        playSound(800, 20);
        lastBtnPress = currentMillis;
      }
      else if (digitalRead(JOY_UP) == LOW) {
        menuSelection = (menuSelection - 1 + 2) % 2;
        drawMenu();
        playSound(800, 20);
        lastBtnPress = currentMillis;
      }
      else if (digitalRead(JOY_BTN) == LOW || digitalRead(JOY_RIGHT) == LOW) {
        playSound(1500, 50);
        lastBtnPress = currentMillis;
        
        if (menuSelection == 0) {
          state = PLAYING;
          initGame();
        } 
        else if (menuSelection == 1) {
          currentLevel++;
          if (currentLevel > 10) currentLevel = 1;
          drawMenu();
        } 
      }
    }
  }

  // ==================================================
  // OBSŁUGA STANU: GRA
  // ==================================================
  else if (state == PLAYING) {
    
    // --- OBSŁUGA KLAWISZY AKCJI ---
    if (currentMillis - lastBtnPress > 300) {
      
      // P1 remains an alternate exit; P4 is the system-wide exit button.
      if (digitalRead(BTN_P4) == LOW || digitalRead(BTN_P1) == LOW) {
        arcadeRequestGameExit();
        return;
      }

      // P2: pause.
      if (digitalRead(BTN_P2) == LOW) {
        isPaused = !isPaused;
        playSound(800, 100);
        lastBtnPress = currentMillis;
        
        if (isPaused) {
          // Rysujemy półprzezroczysty efekt pauzy (czarna ramka z napisem)
          tft.fillRect(60, 130, 120, 50, ST77XX_BLACK);
          tft.drawRect(60, 130, 120, 50, ST77XX_YELLOW);
          tft.setTextColor(ST77XX_YELLOW);
          tft.setTextSize(3);
          tft.setCursor(75, 145);
          tft.print(arcadeText("PAUZA", "PAUSED"));
        } else {
          // Wznowienie - przerysowujemy całą grę, by zmazać napis PAUZA
          redrawGameScreen();
        }
      }

    }

    // Jeśli gra jest zapauzowana, pomijamy logikę ruchu i kolizji
    if (isPaused) return; 

    // --- LOGIKA STEROWANIA I RUCHU WĘŻA ---
    int diffX = 0, diffY = 0;
    if (digitalRead(JOY_RIGHT) == LOW) diffX = 1;
    if (digitalRead(JOY_LEFT) == LOW) diffX = -1;
    if (digitalRead(JOY_DOWN) == LOW) diffY = 1;
    if (digitalRead(JOY_UP) == LOW) diffY = -1;

    int currentJoyDir = -1;
    if (diffX != 0 || diffY != 0) {
      if (abs(diffX) > abs(diffY)) currentJoyDir = (diffX > 0) ? 1 : 3;
      else currentJoyDir = (diffY > 0) ? 2 : 0;
    }

    if (currentJoyDir != -1 && currentJoyDir != lastJoyDir) {
      pushMove(currentJoyDir);
    }
    lastJoyDir = currentJoyDir;

    if (specialActive && (currentMillis - specialSpawnTime > 5000)) {
      specialActive = false;
      tft.fillRect(specialX, specialY, GRID, GRID, C_BG); 
    }

    if (currentMillis - lastMoveTime > gameSpeed) {
      lastMoveTime = currentMillis;

      if (moveQueue[0] != -1) {
        dir = moveQueue[0];
        moveQueue[0] = moveQueue[1];
        moveQueue[1] = -1;
      }

      int nextX = sX[0];
      int nextY = sY[0];
      if (dir == 0) nextY -= GRID; 
      if (dir == 1) nextX += GRID; 
      if (dir == 2) nextY += GRID; 
      if (dir == 3) nextX -= GRID; 

      if (nextX > 220) nextX = 10;
      else if (nextX < 10) nextX = 220;
      if (nextY > 300) nextY = 40;
      else if (nextY < 40) nextY = 300;

      for (int i = 0; i < sLen - 1; i++) {
        if (nextX == sX[i] && nextY == sY[i]) {
          state = GAME_OVER;
          playSound(200, 800);
          updateHighScore();
          return;
        }
      }

      bool ateFood = (nextX == foodX && nextY == foodY);
      bool ateSpecial = (specialActive && nextX == specialX && nextY == specialY);

      int tailX = sX[sLen - 1];
      int tailY = sY[sLen - 1];

      for (int i = sLen - 1; i > 0; i--) {
        sX[i] = sX[i - 1];
        sY[i] = sY[i - 1];
      }
      sX[0] = nextX;
      sY[0] = nextY;

      if (ateFood || ateSpecial) {
        if (sLen < MAX_LEN) {
          sLen++;
          sX[sLen - 1] = tailX;
          sY[sLen - 1] = tailY;
        }
        
        if (ateFood) {
          score += currentLevel; 
          applesEaten++;
          playSound(1800, 40);
          spawnFood();
          if (applesEaten % 5 == 0) spawnSpecialFood();
        } 
        else if (ateSpecial) {
          score += currentLevel * 5;
          specialActive = false;
          playSound(2500, 100);
        }

        tft.fillRect(100, 10, 100, 20, C_BG); 
        tft.setCursor(10, 10);
        tft.print(String(arcadeText("WYNIK: ", "SCORE: ")) + score);
      } 
      else {
        tft.fillRect(tailX, tailY, GRID, GRID, C_BG);
      }

      drawBody(sX[1], sY[1], sX[2], sY[2]);
      drawHead(sX[0], sY[0], dir);
      drawTail(sX[sLen-1], sY[sLen-1], sX[sLen-2], sY[sLen-2]);
    }
  }

  // ==================================================
  // OBSŁUGA STANU: GAME OVER
  // ==================================================
  else if (state == GAME_OVER) {
    tft.fillRect(35, 120, 170, 80, ST77XX_BLACK);
    tft.drawRect(35, 120, 170, 80, ST77XX_RED);
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(3);
    tft.setCursor(45, 135);
    tft.print(arcadeText("KONIEC", "GAME OVER"));
    
    tft.setTextSize(1);
    tft.setTextColor(ST77XX_WHITE);
    tft.setCursor(65, 175);
    tft.print(arcadeText("JOY: MENU", "JOY: MENU"));

    if (digitalRead(JOY_BTN) == LOW || digitalRead(BTN_P1) == LOW || digitalRead(BTN_P4) == LOW) {
      if (currentMillis - lastBtnPress > 500) {
        state = MENU;
        drawMenu();
        lastBtnPress = currentMillis;
      }
    }

  }
}

}  // namespace snake