#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "arcade_runtime.h"
#include "arcade_persistence.h"

namespace tetris {

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
// 2. PINY JOYSTICKA (Cyfrowe)
// ==========================================
#define JOY_UP    17 // F
#define JOY_DOWN  18 // B
#define JOY_LEFT  3  // K
#define JOY_RIGHT 8  // R
#define JOY_BTN   16 // M

// ==========================================
// 3. PINY PRZYCISKÓW AKCJI I AUDIO
// ==========================================
#define BTN_P1 15 // P1: Obrót w lewo
#define BTN_P2 7  // P2: Obrót w prawo
#define BTN_P3 6  // P3: Inwersja kolorów
#define BTN_P4 5  // P4: Wyjście do menu
Adafruit_ST7789 tft = Adafruit_ST7789(&SPI, TFT_CS, TFT_DC, TFT_RST);

#define SCREEN_W 240
#define SCREEN_H 320

// ==========================================
// PARAMETRY PLANSZY TETRIS (10x20 pól)
// ==========================================
const int BLOCK_SIZE = 14;  
const int BOARD_W = 10;     // 140px szerokości
const int BOARD_H = 20;     // 280px wysokości
const int BOARD_X = 50;     // (240 - 140) / 2
const int BOARD_Y = 30;     

byte board[BOARD_H][BOARD_W];

// Kształty Tetromina w układzie 4x4
const byte TETROMINOES[7][4][4] = {
  {{0,0,0,0}, {1,1,1,1}, {0,0,0,0}, {0,0,0,0}}, // I
  {{0,0,1,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}}, // L
  {{1,0,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}}, // J
  {{0,1,1,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}}, // O
  {{1,1,0,0}, {0,1,1,0}, {0,0,0,0}, {0,0,0,0}}, // Z
  {{0,1,1,0}, {1,1,0,0}, {0,0,0,0}, {0,0,0,0}}, // S
  {{0,1,0,0}, {1,1,1,0}, {0,0,0,0}, {0,0,0,0}}  // T
};

uint16_t colors[8] = {
  ST77XX_BLACK,
  ST77XX_CYAN,
  ST77XX_ORANGE,
  ST77XX_BLUE,
  ST77XX_YELLOW,
  ST77XX_RED,
  ST77XX_GREEN,
  ST77XX_MAGENTA
};

// Zmienne mechaniki
int curX = 3, curY = 0;
int curShape = 0, curRot = 0;
int oldX = 3, oldY = 0, oldRot = 0; // Służą do usuwania migotania

enum GameState { MENU, PLAYING, GAME_OVER };
GameState state = MENU;

int menuSelection = 0;
int level = 1;
int score = 0;
int highScore = 0;

void updateHighScore() {
  if (score > highScore) {
    highScore = score;
    arcadeStoreHighScore("tetris_best", static_cast<uint32_t>(highScore));
  }
}

unsigned long lastDropTime = 0;
unsigned long lastInputTime = 0;
unsigned long lastBtnPress = 0;
int baseDropInterval = 800;
bool previousInvertButton = false;
bool previousExitButton = false;

// ==========================================
// FUNKCJE POMOCNICZE
// ==========================================

// Pobieranie wartości bloku uwzględniając rotację
byte getBlockVal(int shape, int rot, int r, int c) {
  if (rot == 0) return TETROMINOES[shape][r][c];
  if (rot == 1) return TETROMINOES[shape][3-c][r];       
  if (rot == 2) return TETROMINOES[shape][3-r][3-c];     
  if (rot == 3) return TETROMINOES[shape][c][3-r];       
  return 0;
}

void drawBlock(int x, int y, uint16_t color) {
  if (y < 0 || y >= BOARD_H || x < 0 || x >= BOARD_W) return;
  tft.fillRect(BOARD_X + (x * BLOCK_SIZE), BOARD_Y + (y * BLOCK_SIZE), BLOCK_SIZE - 1, BLOCK_SIZE - 1, color);
}

void drawPiece(int x, int y, int rot, int shape, uint16_t color) {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (getBlockVal(shape, rot, r, c)) {
        drawBlock(x + c, y + r, color);
      }
    }
  }
}

void redrawBoard() {
  tft.fillRect(BOARD_X, BOARD_Y, BOARD_W * BLOCK_SIZE, BOARD_H * BLOCK_SIZE, ST77XX_BLACK);
  tft.drawRect(BOARD_X - 2, BOARD_Y - 2, (BOARD_W * BLOCK_SIZE) + 3, (BOARD_H * BLOCK_SIZE) + 3, ST77XX_WHITE);
  for (int r = 0; r < BOARD_H; r++) {
    for (int c = 0; c < BOARD_W; c++) {
      if (board[r][c]) {
        drawBlock(c, r, colors[board[r][c]]);
      }
    }
  }
}

void updateScoreUI() {
  tft.fillRect(0, 0, SCREEN_W, 25, ST77XX_BLACK);
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 8);
  tft.print(String(arcadeText("WYNIK: ", "SCORE: ")) + score);
  tft.setCursor(160, 8);
  tft.print(String(arcadeText("POZIOM: ", "LEVEL: ")) + level);
}

// ==========================================
// FIZYKA GRY
// ==========================================
bool checkCollision(int pX, int pY, int shape, int rot) {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (getBlockVal(shape, rot, r, c)) {
        int nextX = pX + c;
        int nextY = pY + r;
        
        // Granice ścian i dna
        if (nextX < 0 || nextX >= BOARD_W || nextY >= BOARD_H) return true;
        // Kolizja z zapisanymi klockami na planszy
        if (nextY >= 0 && board[nextY][nextX]) return true;
      }
    }
  }
  return false;
}

void mergePiece() {
  for (int r = 0; r < 4; r++) {
    for (int c = 0; c < 4; c++) {
      if (getBlockVal(curShape, curRot, r, c)) {
        if (curY + r >= 0 && curY + r < BOARD_H) {
          board[curY + r][curX + c] = curShape + 1;
        }
      }
    }
  }
}

void clearLines() {
  int linesCleared = 0;
  for (int r = BOARD_H - 1; r >= 0; r--) {
    bool full = true;
    for (int c = 0; c < BOARD_W; c++) {
      if (board[r][c] == 0) {
        full = false;
        break;
      }
    }
    if (full) {
      linesCleared++;
      for (int k = r; k > 0; k--) {
        for (int c = 0; c < BOARD_W; c++) {
          board[k][c] = board[k - 1][c];
        }
      }
      for (int c = 0; c < BOARD_W; c++) board[0][c] = 0;
      r++; // Sprawdź ten sam poziom jeszcze raz
    }
  }
  
  if (linesCleared > 0) {
    score += (linesCleared * 100) * level; 
    redrawBoard();
    updateScoreUI();
    arcadePlaySound(linesCleared >= 4 ? 1400 : 1000, 90);
  }
}

void spawnPiece() {
  curShape = random(0, 7);
  curRot = 0;
  curX = 3;
  curY = -1; // Start tuż nad ekranem
  
  oldX = curX; oldY = curY; oldRot = curRot;

  // Od razu wykryj Game Over, jeśli klocek pojawia się w kolizji
  if (checkCollision(curX, curY, curShape, curRot)) {
    state = GAME_OVER;
    updateHighScore();
    arcadePlaySound(180, 140);
  }
}

// ==========================================
// MENU GŁÓWNE
// ==========================================
void drawMenu() {
  tft.fillScreen(ST77XX_BLACK);
  tft.drawRect(5, 5, 230, 310, ST77XX_CYAN);
  
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(3);
  tft.setCursor(45, 30);
  tft.print("TETRIS");
  tft.drawLine(30, 60, 210, 60, ST77XX_CYAN);
  
  tft.setTextSize(2);
  String options[2] = {
      arcadeText("NOWA GRA", "NEW GAME"),
      String(arcadeText("POZIOM: ", "LEVEL: ")) + level};
  
  for (int i = 0; i < 2; i++) {
    int yPos = 120 + (i * 45);
    if (i == menuSelection) {
      tft.fillRect(20, yPos - 5, 200, 35, tft.color565(80, 80, 80));
      tft.setTextColor(ST77XX_WHITE);
    } else {
      tft.setTextColor(ST77XX_WHITE);
    }
    tft.setCursor(35, yPos);
    tft.print(options[i]);
  }
  
  tft.setTextColor(ST77XX_ORANGE);
  tft.setTextSize(1);
  tft.setCursor(55, 280);
  tft.print(String(arcadeText("REKORD: ", "HIGH SCORE: ")) + highScore);
}

void initGame() {
  score = 0;
  memset(board, 0, sizeof(board));
  baseDropInterval = 1000 - (level * 80); 
  if(baseDropInterval < 100) baseDropInterval = 100;
  
  tft.fillScreen(ST77XX_BLACK);
  redrawBoard();
  updateScoreUI();
  spawnPiece();
  
}

// ==========================================
// SETUP
// ==========================================
void begin() {
  state = MENU;
  menuSelection = 0;
  highScore = static_cast<int>(arcadeLoadUInt("tetris_best", 0));
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
  
  randomSeed(analogRead(0));

  drawMenu();
}

// ==========================================
// GŁÓWNA PĘTLA
// ==========================================
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

  // --- MENU ---
  if (state == MENU) {
    if (currentMillis - lastBtnPress > 200) {
      if (digitalRead(JOY_DOWN) == LOW || digitalRead(JOY_UP) == LOW) {
        menuSelection = (menuSelection == 0) ? 1 : 0;
        drawMenu(); arcadePlaySound(700, 25); lastBtnPress = currentMillis;
      }
      // Zmiana poziomu joystickiem L/R
      else if (digitalRead(JOY_RIGHT) == LOW && menuSelection == 1) { 
        level++; if(level > 10) level = 1;
        drawMenu(); arcadePlaySound(850, 35); lastBtnPress = currentMillis;
      }
      else if (digitalRead(JOY_LEFT) == LOW && menuSelection == 1) { 
        level--; if(level < 1) level = 10;
        drawMenu(); arcadePlaySound(650, 35); lastBtnPress = currentMillis;
      }
      else if (digitalRead(JOY_BTN) == LOW) {
        if (menuSelection == 0) {
          state = PLAYING;
          initGame();
          arcadePlaySound(1100, 55);
        }
        lastBtnPress = currentMillis;
      }
    }
  }
  
  // --- GRA ---
  else if (state == PLAYING) {
    
    // Obsługa przycisków akcji (Debounce)
    if (currentMillis - lastBtnPress > 150) {
      
      // P1: Obrót Lewo
      if (digitalRead(BTN_P1) == LOW) {
        int nextRot = (curRot + 3) % 4; 
        if (!checkCollision(curX, curY, curShape, nextRot)) {
          curRot = nextRot;
          arcadePlaySound(1200, 20);
        }
        lastBtnPress = currentMillis;
      }
      // P2: Obrót Prawo
      else if (digitalRead(BTN_P2) == LOW) {
        int nextRot = (curRot + 1) % 4; 
        if (!checkCollision(curX, curY, curShape, nextRot)) {
          curRot = nextRot;
          arcadePlaySound(1300, 20);
        }
        lastBtnPress = currentMillis;
      }
      // P4: Wyjście z gry
      else if (digitalRead(BTN_P4) == LOW) {
        updateHighScore();
        arcadeRequestGameExit();
        return;
      }

    }

    // Sterowanie joystickiem L/R
    if (currentMillis - lastInputTime > 120) {
      if (digitalRead(JOY_LEFT) == LOW) {
        if (!checkCollision(curX - 1, curY, curShape, curRot)) {
          curX--;
          lastInputTime = currentMillis;
        }
      }

      else if (digitalRead(JOY_RIGHT) == LOW) {
        if (!checkCollision(curX + 1, curY, curShape, curRot)) {
          curX++;
          lastInputTime = currentMillis;
        }
      }
    }

    // Szybsze opadanie joystickiem w DÓŁ
    int currentDropInterval = (digitalRead(JOY_DOWN) == LOW) ? 40 : baseDropInterval;

    // Grawitacja
    if (currentMillis - lastDropTime > currentDropInterval) {
      lastDropTime = currentMillis;

      if (!checkCollision(curX, curY + 1, curShape, curRot)) {
        curY++;
      } else {
        // Klocek ląduje na planszy
        mergePiece();
        clearLines();
        spawnPiece();
      }
    }

    // RYSOWANIE RÓŻNICOWE (Brak mrugania ekranu!)
    if (oldX != curX || oldY != curY || oldRot != curRot) {
      // 1. Wymaż starą klatkę
      drawPiece(oldX, oldY, oldRot, curShape, ST77XX_BLACK);
      // 2. Narysuj nową klatkę
      drawPiece(curX, curY, curRot, curShape, colors[curShape + 1]);
      
      oldX = curX; oldY = curY; oldRot = curRot;
    }
  }

  // --- GAME OVER ---
  else if (state == GAME_OVER) {
    tft.fillRect(40, 130, 160, 60, ST77XX_BLACK);
    tft.drawRect(40, 130, 160, 60, ST77XX_RED);
    tft.setTextColor(ST77XX_RED);
    tft.setTextSize(2);
    tft.setCursor(60, 145);
    tft.println(arcadeText("KONIEC GRY", "GAME OVER"));
    tft.setTextSize(1);
    tft.setCursor(70, 170);
    tft.setTextColor(ST77XX_WHITE);
    tft.print(arcadeText("JOY: MENU", "JOY: MENU"));
    
    if (digitalRead(JOY_BTN) == LOW && currentMillis - lastBtnPress > 500) {
      state = MENU;
      drawMenu();
      lastBtnPress = currentMillis;
    }
  }
}

}  // namespace tetris