#include <Arduino.h>
#include <Adafruit_ST7789.h>
#include <math.h>
#include <string.h>

#include "arcade_persistence.h"
#include "arcade_runtime.h"
#include "games.h"
#include "pins.h"

namespace arkanoid {
namespace {

constexpr int16_t screenWidth = 240;
constexpr int16_t screenHeight = 320;
constexpr int16_t brickColumns = 10;
constexpr int16_t brickRows = 5;
constexpr int16_t brickWidth = 21;
constexpr int16_t brickHeight = 11;
constexpr int16_t brickPitchX = 22;
constexpr int16_t brickPitchY = 16;
constexpr int16_t brickStartX = 10;
constexpr int16_t brickStartY = 52;
constexpr int16_t paddleWidth = 48;
constexpr int16_t paddleHeight = 7;
constexpr int16_t paddleY = 294;
constexpr float ballRadius = 4.0f;

enum class State : uint8_t { Ready, Playing, Paused, GameOver };

State state = State::Ready;
bool bricks[brickRows][brickColumns] = {};
int16_t paddleX = (screenWidth - paddleWidth) / 2;
float paddlePosition = static_cast<float>((screenWidth - paddleWidth) / 2);
float ballX = screenWidth / 2.0f;
float ballY = paddleY - ballRadius - 1.0f;
float velocityX = 105.0f;
float velocityY = -175.0f;
uint32_t score = 0;
uint32_t highScore = 0;
uint8_t lives = 3;
uint8_t level = 1;
uint32_t lastUpdateMs = 0;
bool previousLaunchPressed = false;
bool previousPausePressed = false;
bool previousInvertPressed = false;
bool previousExitPressed = false;

Adafruit_ST7789& display() {
  return arcadeSharedDisplay();
}

void updateHighScore() {
  if (score > highScore) {
    highScore = score;
    arcadeStoreHighScore("arkanoid_best", highScore);
  }
}

uint16_t brickColor(int16_t row) {
  constexpr uint16_t colors[] = {
      ST77XX_RED, ST77XX_ORANGE, ST77XX_YELLOW,
      ST77XX_GREEN, ST77XX_CYAN,
  };
  return colors[row % (sizeof(colors) / sizeof(colors[0]))];
}

void drawHud() {
  display().fillRect(0, 0, screenWidth, 32, ST77XX_BLACK);
  display().setTextSize(1);
  display().setTextColor(ST77XX_CYAN, ST77XX_BLACK);
  display().setCursor(7, 5);
  display().print(arcadeText("ARKANOID WYNIK ", "ARKANOID SCORE "));
  display().print(score);
  display().setCursor(7, 19);
  display().print(arcadeText("REKORD ", "BEST "));
  display().print(highScore);
  display().setCursor(153, 19);
  display().print(arcadeText("ZYCIA ", "LIVES "));
  display().print(lives);
  display().print("  L");
  display().print(level);
}

void drawBricks() {
  for (int16_t row = 0; row < brickRows; ++row) {
    for (int16_t column = 0; column < brickColumns; ++column) {
      const int16_t x = brickStartX + column * brickPitchX;
      const int16_t y = brickStartY + row * brickPitchY;
      if (bricks[row][column]) {
        display().fillRoundRect(x, y, brickWidth, brickHeight, 2,
                               brickColor(row));
        display().drawFastHLine(x + 2, y + 2, brickWidth - 4, ST77XX_WHITE);
      } else {
        display().fillRect(x, y, brickWidth, brickHeight, ST77XX_BLACK);
      }
    }
  }
}

void drawPaddle() {
  display().fillRoundRect(paddleX, paddleY, paddleWidth, paddleHeight, 3,
                          ST77XX_CYAN);
  display().drawFastHLine(paddleX + 5, paddleY + 1, paddleWidth - 10,
                          ST77XX_WHITE);
}

void drawBall() {
  display().fillCircle(static_cast<int16_t>(lroundf(ballX)),
                       static_cast<int16_t>(lroundf(ballY)),
                       static_cast<int16_t>(ballRadius), ST77XX_YELLOW);
}

void drawScene() {
  display().fillScreen(ST77XX_BLACK);
  display().drawFastHLine(0, 32, screenWidth, ST77XX_BLUE);
  drawHud();
  drawBricks();
  display().drawRect(1, 34, screenWidth - 2, screenHeight - 36, ST77XX_BLUE);
  drawPaddle();
  drawBall();
}

void drawMessage(const char* title, const char* subtitle) {
  display().fillRoundRect(24, 137, 192, 66, 8, ST77XX_BLACK);
  display().drawRoundRect(24, 137, 192, 66, 8, ST77XX_CYAN);
  display().setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  display().setTextSize(2);
  display().setCursor(120 - static_cast<int16_t>(strlen(title) * 6), 150);
  display().print(title);
  display().setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  display().setTextSize(1);
  display().setCursor(120 - static_cast<int16_t>(strlen(subtitle) * 3), 179);
  display().print(subtitle);
}

void resetBall() {
  ballX = paddlePosition + paddleWidth / 2.0f;
  ballY = paddleY - ballRadius - 1.0f;
  const float angle = (random(0, 2) == 0 ? -1.0f : 1.0f);
  velocityX = angle * (95.0f + level * 7.0f);
  velocityY = -(175.0f + level * 9.0f);
}

void positionBallOnPaddle() {
  ballX = paddlePosition + paddleWidth / 2.0f;
  ballY = paddleY - ballRadius - 1.0f;
}

void resetBricks() {
  for (int16_t row = 0; row < brickRows; ++row) {
    for (int16_t column = 0; column < brickColumns; ++column) {
      bricks[row][column] = true;
    }
  }
}

void startNewGame() {
  score = 0;
  lives = 3;
  level = 1;
  paddleX = (screenWidth - paddleWidth) / 2;
  paddlePosition = static_cast<float>(paddleX);
  resetBricks();
  resetBall();
  state = State::Ready;
  drawScene();
  drawMessage("ARKANOID", arcadeText("A: START", "A: START"));
}

void launchBall() {
  if (state != State::Ready) return;
  state = State::Playing;
  lastUpdateMs = millis();
  drawScene();
}

void finishLevel() {
  ++level;
  resetBricks();
  resetBall();
  state = State::Ready;
  drawScene();
  drawMessage(arcadeText("POZIOM!", "LEVEL UP!"), "A: START");
}

void loseLife() {
  if (lives > 0) --lives;
  drawHud();
  if (lives == 0) {
    updateHighScore();
    state = State::GameOver;
    drawMessage(arcadeText("KONIEC GRY", "GAME OVER"),
                arcadeText("A: JESZCZE RAZ", "A: PLAY AGAIN"));
  } else {
    resetBall();
    state = State::Ready;
    drawPaddle();
    drawBall();
    drawMessage(arcadeText("STRATA ZYCIA", "LIFE LOST"),
                arcadeText("A: KONTYNUUJ", "A: CONTINUE"));
  }
}

void movePaddle(float deltaSeconds) {
  const bool left = digitalRead(pins::joy_left) == LOW;
  const bool right = digitalRead(pins::joy_right) == LOW;
  const int16_t oldX = paddleX;
  const float speed = 250.0f;
  if (left != right) {
    paddlePosition += (right ? 1.0f : -1.0f) * speed * deltaSeconds;
  }
  if (paddlePosition < 3) paddlePosition = 3;
  if (paddlePosition > screenWidth - paddleWidth - 3) {
    paddlePosition = screenWidth - paddleWidth - 3;
  }
  paddleX = static_cast<int16_t>(lroundf(paddlePosition));
  if (paddleX != oldX) {
    display().fillRect(oldX, paddleY, paddleWidth, paddleHeight, ST77XX_BLACK);
    drawPaddle();
    if (state == State::Ready) {
      display().fillCircle(static_cast<int16_t>(lroundf(ballX)),
                           static_cast<int16_t>(lroundf(ballY)),
                           static_cast<int16_t>(ballRadius), ST77XX_BLACK);
      positionBallOnPaddle();
      drawBall();
    }
  }
}

void updateBall(float deltaSeconds) {
  const float oldX = ballX;
  const float oldY = ballY;
  ballX += velocityX * deltaSeconds;
  ballY += velocityY * deltaSeconds;

  if (ballX - ballRadius < 3) {
    ballX = 3 + ballRadius;
    velocityX = fabsf(velocityX);
  } else if (ballX + ballRadius > screenWidth - 3) {
    ballX = screenWidth - 3 - ballRadius;
    velocityX = -fabsf(velocityX);
  }
  if (ballY - ballRadius < 36) {
    ballY = 36 + ballRadius;
    velocityY = fabsf(velocityY);
  }

  if (velocityY > 0 && oldY + ballRadius <= paddleY &&
      ballY + ballRadius >= paddleY && ballX >= paddleX - ballRadius &&
      ballX <= paddleX + paddleWidth + ballRadius) {
    const float offset = (ballX - (paddleX + paddleWidth / 2.0f)) /
                         (paddleWidth / 2.0f);
    const float speed = sqrtf(velocityX * velocityX + velocityY * velocityY);
    const float horizontal = offset * speed * 0.82f;
    velocityX = horizontal;
    velocityY = -sqrtf(speed * speed - horizontal * horizontal);
    ballY = paddleY - ballRadius;
    arcadePlaySound(720, 18);
  }

  bool hitBrick = false;
  for (int16_t row = 0; row < brickRows && !hitBrick; ++row) {
    for (int16_t column = 0; column < brickColumns; ++column) {
      if (!bricks[row][column]) continue;
      const int16_t x = brickStartX + column * brickPitchX;
      const int16_t y = brickStartY + row * brickPitchY;
      if (ballX + ballRadius < x || ballX - ballRadius > x + brickWidth ||
          ballY + ballRadius < y || ballY - ballRadius > y + brickHeight) {
        continue;
      }

      bricks[row][column] = false;
      display().fillRect(x, y, brickWidth, brickHeight, ST77XX_BLACK);
      score += static_cast<uint32_t>((brickRows - row) * 10);
      updateHighScore();
      drawHud();
      arcadePlaySound(static_cast<uint16_t>(1050 + row * 120), 24);
      if (oldX + ballRadius <= x || oldX - ballRadius >= x + brickWidth) {
        velocityX = -velocityX;
      } else {
        velocityY = -velocityY;
      }
      hitBrick = true;
      break;
    }
  }

  bool bricksRemain = false;
  for (const auto& row : bricks) {
    for (bool brick : row) bricksRemain |= brick;
  }
  if (!bricksRemain) {
    finishLevel();
    return;
  }

  if (ballY - ballRadius > screenHeight) {
    display().fillCircle(static_cast<int16_t>(lroundf(oldX)),
                         static_cast<int16_t>(lroundf(oldY)),
                         static_cast<int16_t>(ballRadius), ST77XX_BLACK);
    loseLife();
    return;
  }

  display().fillCircle(static_cast<int16_t>(lroundf(oldX)),
                       static_cast<int16_t>(lroundf(oldY)),
                       static_cast<int16_t>(ballRadius), ST77XX_BLACK);
  drawBall();
}

}  // namespace

void begin() {
  pinMode(pins::joy_left, INPUT_PULLUP);
  pinMode(pins::joy_right, INPUT_PULLUP);
  pinMode(pins::joy_button, INPUT_PULLUP);
  pinMode(pins::button_p1, INPUT_PULLUP);
  pinMode(pins::button_p2, INPUT_PULLUP);
  pinMode(pins::button_p3, INPUT_PULLUP);
  pinMode(pins::button_p4, INPUT_PULLUP);

  highScore = arcadeLoadUInt("arkanoid_best", 0);
  previousLaunchPressed = digitalRead(pins::button_p1) == LOW ||
                         digitalRead(pins::joy_button) == LOW;
  previousPausePressed = digitalRead(pins::button_p2) == LOW;
  previousInvertPressed = digitalRead(pins::button_p3) == LOW;
  previousExitPressed = digitalRead(pins::button_p4) == LOW;

  display().setRotation(0);
  display().invertDisplay(arcadeDisplayInverted());
  randomSeed(micros());
  startNewGame();
}

void tick() {
  const bool invertPressed = digitalRead(pins::button_p3) == LOW;
  if (invertPressed && !previousInvertPressed) {
    arcadeToggleDisplayInversion();
    display().invertDisplay(arcadeDisplayInverted());
  }
  previousInvertPressed = invertPressed;

  const bool exitPressed = digitalRead(pins::button_p4) == LOW;
  if (exitPressed && !previousExitPressed) {
    arcadeRequestGameExit();
    return;
  }
  previousExitPressed = exitPressed;

  const bool launchPressed = digitalRead(pins::button_p1) == LOW ||
                             digitalRead(pins::joy_button) == LOW;
  const bool launchEdge = launchPressed && !previousLaunchPressed;
  previousLaunchPressed = launchPressed;
  const bool pausePressed = digitalRead(pins::button_p2) == LOW;
  if (pausePressed && !previousPausePressed) {
    if (state == State::Playing) {
      state = State::Paused;
      drawMessage(arcadeText("PAUZA", "PAUSED"),
                  arcadeText("B: WZNOW", "B: RESUME"));
    } else if (state == State::Paused) {
      state = State::Playing;
      lastUpdateMs = millis();
      drawScene();
    }
  }
  previousPausePressed = pausePressed;

  const uint32_t now = millis();
  const float deltaSeconds = min(now - lastUpdateMs, 35U) / 1000.0f;
  lastUpdateMs = now;
  if (state == State::Ready || state == State::Playing) {
    movePaddle(deltaSeconds);
  }

  if (launchEdge && state == State::GameOver) {
    startNewGame();
    previousLaunchPressed = true;
    return;
  }
  if (launchEdge && state == State::Ready) {
    launchBall();
    return;
  }
  if (state != State::Playing || deltaSeconds <= 0.0f) return;
  updateBall(deltaSeconds);
}

}  // namespace arkanoid
