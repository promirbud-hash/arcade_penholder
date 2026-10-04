#include <Arduino.h>
#include <Preferences.h>

#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

#include "arcade_runtime.h"
#include "pins.h"

namespace tamagotchi {
namespace {

constexpr uint32_t kSaveIntervalMs = 5UL * 60 * 1000;
constexpr uint32_t kHungerIntervalSeconds = 20UL * 60;
constexpr uint32_t kHappyIntervalSeconds = 30UL * 60;
constexpr uint32_t kEnergyIntervalSeconds = 30UL * 60;
constexpr uint32_t kCleanIntervalSeconds = 45UL * 60;
constexpr uint32_t kHealthIntervalSeconds = 15UL * 60;
constexpr uint32_t kMagic = 0x54414D41;
constexpr uint16_t kVersion = 1;
constexpr uint8_t kPetCount = 5;
constexpr uint8_t kActionCount = 7;
constexpr uint8_t kTrainingRounds = 3;
constexpr uint32_t kTrainingCueMs = 650;
constexpr uint32_t kTrainingInputMs = 2200;
constexpr uint16_t kDarkGray = 0x7BEF;

struct PetState {
  uint32_t ageSeconds;
  uint32_t hungerElapsed;
  uint32_t happyElapsed;
  uint32_t energyElapsed;
  uint32_t cleanElapsed;
  uint32_t healthElapsed;
  uint16_t careCount;
  uint8_t hunger;
  uint8_t happiness;
  uint8_t energy;
  uint8_t cleanliness;
  uint8_t health;
};

struct SaveData {
  uint32_t magic;
  uint16_t version;
  PetState pets[kPetCount];
};

enum class Action : uint8_t { Feed, Play, Clean, Rest, Treat, Train, Shop };
enum class Screen : uint8_t { Pet, TrainingCue, TrainingInput, TrainingResult, Shop };

Adafruit_ST7789& display = arcadeSharedDisplay();
Preferences preferences;
SaveData saveData{};
Screen screen = Screen::Pet;

constexpr uint8_t kShopCosts[3] = {3, 4, 6};
constexpr char kDirectionMarks[4] = {'^', '>', 'v', '<'};

uint8_t selectedPet = 0;
uint8_t selectedAction = 0;
uint8_t selectedShopItem = 0;
uint8_t trainingSequence[4] = {};
uint8_t trainingRound = 1;
uint8_t trainingStep = 0;
uint8_t trainingBestRound = 0;
uint32_t trainingDeadlineMs = 0;
uint32_t trainingCueUntilMs = 0;
uint32_t coins = 0;
uint32_t lastServiceMs = 0;
uint32_t lastSaveMs = 0;
uint32_t lastDrawMs = 0;
uint32_t elapsedRemainderMs = 0;
bool storageReady = false;
bool dirty = false;
bool previousLeft = false;
bool previousRight = false;
bool previousUp = false;
bool previousDown = false;
bool previousJoyButton = false;
bool previousP1 = false;
bool previousP2 = false;
bool previousP3 = false;
bool previousP4 = false;

const char* petName(uint8_t pet) {
  static const char* const polish[] = {
      "KOTEK", "PIESEK", "KROLIK", "SZEF", "DYREKTORKA"};
  static const char* const english[] = {
      "CAT", "DOG", "RABBIT", "BOSS", "DIRECTOR"};
  return arcadeIsPolish() ? polish[pet] : english[pet];
}

const char* actionName(uint8_t action) {
  static const char* const polish[] = {
      "KARMIJ", "ZABAWA", "SPRZAT.", "ODPOCZYNEK", "LEKARSTWO",
      "TRENING", "SKLEP"};
  static const char* const english[] = {
      "FEED", "PLAY", "CLEAN", "REST", "MEDICINE", "TRAIN", "SHOP"};
  return arcadeIsPolish() ? polish[action] : english[action];
}

const char* shopName(uint8_t item) {
  static const char* const polish[] = {"SMACZEK", "ZABAWKA", "APTECZKA"};
  static const char* const english[] = {"TREAT", "TOY", "MEDICINE"};
  return arcadeIsPolish() ? polish[item] : english[item];
}

const char* directionName(uint8_t direction) {
  static const char* const polish[] = {"GORA", "PRAWO", "DOL", "LEWO"};
  static const char* const english[] = {"UP", "RIGHT", "DOWN", "LEFT"};
  return arcadeIsPolish() ? polish[direction] : english[direction];
}

PetState makeNewPet() {
  PetState pet{};
  pet.hunger = 80;
  pet.happiness = 80;
  pet.energy = 80;
  pet.cleanliness = 80;
  pet.health = 100;
  return pet;
}

void initializeDefaults() {
  saveData = {};
  saveData.magic = kMagic;
  saveData.version = kVersion;
  for (PetState& pet : saveData.pets) pet = makeNewPet();
  dirty = true;
}

bool validSaveData(const SaveData& data) {
  if (data.magic != kMagic || data.version != kVersion) return false;
  for (const PetState& pet : data.pets) {
    if (pet.hunger > 100 || pet.happiness > 100 || pet.energy > 100 ||
        pet.cleanliness > 100 || pet.health > 100 ||
        pet.hungerElapsed >= kHungerIntervalSeconds ||
        pet.happyElapsed >= kHappyIntervalSeconds ||
        pet.energyElapsed >= kEnergyIntervalSeconds ||
        pet.cleanElapsed >= kCleanIntervalSeconds ||
        pet.healthElapsed >= kHealthIntervalSeconds) {
      return false;
    }
  }
  return true;
}

bool saveTrainingRecord() {
  if (!storageReady) {
    Serial.println("Tamagotchi NVS unavailable; training record remains volatile.");
    return false;
  }
  if (preferences.putUChar("train_best", trainingBestRound) !=
      sizeof(trainingBestRound)) {
    Serial.println("Tamagotchi NVS save failed for training record.");
    return false;
  }
  return true;
}

bool saveToNvs() {
  if (!storageReady) return false;
  lastSaveMs = millis();
  const size_t written =
      preferences.putBytes("state", &saveData, sizeof(saveData));
  if (written != sizeof(saveData)) {
    Serial.printf("Tamagotchi NVS save failed: wrote %u of %u bytes.\n",
                  static_cast<unsigned>(written),
                  static_cast<unsigned>(sizeof(saveData)));
    return false;
  }
  dirty = false;
  lastSaveMs = millis();
  return true;
}

bool saveCoins() {
  if (!storageReady) {
    Serial.println("Tamagotchi NVS unavailable; coin balance remains volatile.");
    return false;
  }
  if (preferences.putUInt("coins", coins) != sizeof(coins)) {
    Serial.println("Tamagotchi NVS save failed for coin balance.");
    return false;
  }
  return true;
}

void addCoins(uint32_t amount) {
  if (UINT32_MAX - coins < amount) {
    coins = UINT32_MAX;
  } else {
    coins += amount;
  }
  saveCoins();
}

bool pressed(int pin) {
  return digitalRead(pin) == LOW;
}

bool pressedEdge(int pin, bool& previous) {
  const bool current = pressed(pin);
  const bool edge = current && !previous;
  previous = current;
  return edge;
}

void applyElapsed(PetState& pet, uint32_t elapsedSeconds) {
  pet.ageSeconds += elapsedSeconds;
  uint32_t remaining = elapsedSeconds;
  while (remaining > 0) {
    uint32_t step = remaining;
    if (kHungerIntervalSeconds - pet.hungerElapsed < step) {
      step = kHungerIntervalSeconds - pet.hungerElapsed;
    }
    if (kHappyIntervalSeconds - pet.happyElapsed < step) {
      step = kHappyIntervalSeconds - pet.happyElapsed;
    }
    if (kEnergyIntervalSeconds - pet.energyElapsed < step) {
      step = kEnergyIntervalSeconds - pet.energyElapsed;
    }
    if (kCleanIntervalSeconds - pet.cleanElapsed < step) {
      step = kCleanIntervalSeconds - pet.cleanElapsed;
    }
    if (kHealthIntervalSeconds - pet.healthElapsed < step) {
      step = kHealthIntervalSeconds - pet.healthElapsed;
    }

    pet.hungerElapsed += step;
    pet.happyElapsed += step;
    pet.energyElapsed += step;
    pet.cleanElapsed += step;
    pet.healthElapsed += step;
    remaining -= step;

    if (pet.hungerElapsed >= kHungerIntervalSeconds) {
      pet.hungerElapsed -= kHungerIntervalSeconds;
      if (pet.hunger > 0) --pet.hunger;
    }
    if (pet.happyElapsed >= kHappyIntervalSeconds) {
      pet.happyElapsed -= kHappyIntervalSeconds;
      if (pet.happiness > 0) --pet.happiness;
    }
    if (pet.energyElapsed >= kEnergyIntervalSeconds) {
      pet.energyElapsed -= kEnergyIntervalSeconds;
      if (pet.energy > 0) --pet.energy;
    }
    if (pet.cleanElapsed >= kCleanIntervalSeconds) {
      pet.cleanElapsed -= kCleanIntervalSeconds;
      if (pet.cleanliness > 0) --pet.cleanliness;
    }

    if (pet.healthElapsed >= kHealthIntervalSeconds) {
      pet.healthElapsed -= kHealthIntervalSeconds;
      const bool neglected = pet.hunger == 0 || pet.happiness == 0 ||
                             pet.energy == 0 || pet.cleanliness == 0;
      if (neglected) {
        if (pet.health > 0) --pet.health;
      } else if (pet.health < 100) {
        ++pet.health;
      }
    }
  }
}

void drawBar(int16_t x, int16_t y, int16_t width, uint8_t value,
             uint16_t color) {
  display.drawRect(x, y, width, 10, kDarkGray);
  const int16_t fillWidth = (width - 2) * value / 100;
  if (fillWidth > 0) display.fillRect(x + 1, y + 1, fillWidth, 8, color);
}

void drawPetSprite(uint8_t petIndex, int16_t centerX, int16_t centerY) {
  const uint16_t fur = petIndex == 0 ? 0xFD20 :
                       petIndex == 1 ? 0xC618 :
                       petIndex == 2 ? 0xF81F :
                       petIndex == 3 ? 0x07FF : 0xFBE0;
  display.fillRoundRect(centerX - 27, centerY - 22, 54, 44, 12, fur);
  if (petIndex <= 2) {
    display.fillTriangle(centerX - 20, centerY - 16, centerX - 17,
                         centerY - 34, centerX - 4, centerY - 18, fur);
    display.fillTriangle(centerX + 20, centerY - 16, centerX + 17,
                         centerY - 34, centerX + 4, centerY - 18, fur);
    display.fillCircle(centerX - 10, centerY - 3, 3, ST77XX_BLACK);
    display.fillCircle(centerX + 10, centerY - 3, 3, ST77XX_BLACK);
    display.fillTriangle(centerX, centerY + 2, centerX - 4, centerY + 7,
                         centerX + 4, centerY + 7, ST77XX_RED);
  } else {
    display.fillRect(centerX - 14, centerY - 10, 7, 7, ST77XX_BLACK);
    display.fillRect(centerX + 7, centerY - 10, 7, 7, ST77XX_BLACK);
    display.drawFastHLine(centerX - 8, centerY + 9, 16, ST77XX_BLACK);
    display.fillRect(centerX - 20, centerY + 16, 40, 7, ST77XX_BLUE);
    if (petIndex == 3) {
      display.fillRect(centerX - 23, centerY - 22, 46, 7, ST77XX_BLACK);
      display.fillRect(centerX - 15, centerY - 30, 30, 9, ST77XX_BLACK);
      display.drawFastVLine(centerX, centerY + 16, 7, ST77XX_YELLOW);
    } else {
      display.fillCircle(centerX, centerY - 27, 6, ST77XX_RED);
    }
  }
}

const char* growthStage(uint32_t ageSeconds) {
  if (ageSeconds < 60UL * 60) return arcadeText("MALUCH", "BABY");
  if (ageSeconds < 4UL * 60 * 60) return "JUNIOR";
  return arcadeText("DOROSLY", "ADULT");
}

void drawScreen() {
  display.fillScreen(ST77XX_BLACK);
  display.setTextSize(2);
  display.setTextColor(ST77XX_CYAN);
  display.setCursor(54, 12);
  display.print("TAMAGOCHI");
  display.drawFastHLine(12, 39, 216, ST77XX_MAGENTA);

  PetState& pet = saveData.pets[selectedPet];
  display.setTextSize(2);
  display.setTextColor(ST77XX_YELLOW);
  int16_t x1, y1;
  uint16_t nameWidth, nameHeight;
  display.getTextBounds(petName(selectedPet), 0, 0, &x1, &y1,
                        &nameWidth, &nameHeight);
  display.setCursor((240 - nameWidth) / 2, 51);
  display.print(petName(selectedPet));
  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(8, 75);
  display.print(growthStage(pet.ageSeconds));
  display.print(arcadeText("  WIEK ", "  AGE "));
  display.print(pet.ageSeconds / 3600);
  display.print("h");
  display.setCursor(128, 75);
  display.print(arcadeText("MON ", "COIN "));
  display.print(coins);
  display.setCursor(8, 90);
  display.print(arcadeText("OPIEKA ", "CARE "));
  display.print(pet.careCount);

  drawPetSprite(selectedPet, 120, 140);

  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(19, 181);
  display.print(arcadeText("GLOD", "HUNGER"));
  display.setCursor(128, 181);
  display.print(arcadeText("HUMOR", "MOOD"));
  drawBar(18, 194, 96, pet.hunger, ST77XX_ORANGE);
  drawBar(126, 194, 96, pet.happiness, ST77XX_GREEN);
  display.setCursor(19, 216);
  display.print(arcadeText("ENERGIA", "ENERGY"));
  display.setCursor(128, 216);
  display.print(arcadeText("CZYSTOSC", "CLEAN"));
  drawBar(18, 229, 96, pet.energy, ST77XX_CYAN);
  drawBar(126, 229, 96, pet.cleanliness, ST77XX_BLUE);
  display.setCursor(19, 251);
  display.print(arcadeText("ZDROWIE", "HEALTH"));
  drawBar(70, 249, 152, pet.health, ST77XX_RED);

  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(15, 275);
  display.print(arcadeText("AKCJA: ", "ACTION: "));
  display.print(actionName(selectedAction));
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(8, 294);
  display.print(arcadeText("L/P: POSTAC  G/D/B: AKCJA",
                           "L/R: PET  U/D/B: ACTION"));
  display.setCursor(8, 308);
  display.print(arcadeText("JOY/A: WYBOR  B: AKCJA  Z: MENU",
                           "JOY/A: SELECT  B: ACT  Z: MENU"));
  lastDrawMs = millis();
}

void drawTrainingScreen(const char* title, const char* instruction,
                        char mark, uint8_t step, uint8_t length) {
  display.fillScreen(display.color565(11, 17, 39));
  display.setTextColor(ST77XX_CYAN);
  display.setTextSize(2);
  display.setCursor(36, 18);
  display.print(arcadeText("TRENING", "TRAINING"));
  display.drawFastHLine(18, 47, 204, ST77XX_MAGENTA);

  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(18, 66);
  display.print(petName(selectedPet));
  display.print(arcadeText("  RUNDA ", "  ROUND "));
  display.print(trainingRound);
  display.print("/");
  display.print(kTrainingRounds);
  display.setCursor(18, 83);
  display.print(arcadeText("SEKWENCJA ", "SEQUENCE "));
  display.print(step + 1);
  display.print("/");
  display.print(length);

  display.setTextColor(ST77XX_YELLOW);
  display.setTextSize(6);
  display.setCursor(100, 119);
  display.print(mark);
  display.setTextSize(2);
  display.setCursor(120 - static_cast<int16_t>(strlen(title) * 6), 206);
  display.print(title);
  display.setTextSize(1);
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(120 - static_cast<int16_t>(strlen(instruction) * 3), 239);
  display.print(instruction);
  display.setCursor(24, 285);
  display.print(arcadeText("TRAFNA SEKWENCJA = MONETY",
                           "CORRECT SEQUENCE = COINS"));
  display.setCursor(58, 305);
  display.print(arcadeText("Z: POWROT DO PUPILA", "Z: BACK TO PET"));
  lastDrawMs = millis();
}

void drawShop() {
  display.fillScreen(ST77XX_BLACK);
  display.setTextColor(ST77XX_CYAN);
  display.setTextSize(2);
  display.setCursor(78, 18);
  display.print(arcadeText("SKLEP", "SHOP"));
  display.drawFastHLine(18, 47, 204, ST77XX_MAGENTA);
  display.setTextSize(1);
  display.setTextColor(ST77XX_YELLOW);
  display.setCursor(18, 63);
  display.print(petName(selectedPet));
  display.setCursor(154, 63);
  display.print(arcadeText("MON: ", "COINS: "));
  display.print(coins);

  for (uint8_t i = 0; i < 3; ++i) {
    const int16_t y = 105 + i * 48;
    if (i == selectedShopItem) {
      display.fillRoundRect(18, y - 8, 204, 38, 5, display.color565(40, 52, 76));
      display.drawRoundRect(18, y - 8, 204, 38, 5, ST77XX_CYAN);
    }
    display.setTextColor(i == selectedShopItem ? ST77XX_WHITE : kDarkGray);
    display.setCursor(30, y);
    display.print(shopName(i));
    display.setCursor(174, y);
    display.print(kShopCosts[i]);
    display.print(arcadeText(" MON", " COIN"));
  }
  display.setTextColor(ST77XX_WHITE);
  display.setCursor(13, 278);
  display.print(arcadeText("G/D: WYBOR  A: KUP  B: POWROT",
                           "U/D: SELECT  A: BUY  B: BACK"));
  display.setCursor(47, 302);
  display.print(arcadeText("Z: MENU GLOWNE", "Z: MAIN MENU"));
  lastDrawMs = millis();
}

void startTraining() {
  trainingRound = 1;
  trainingStep = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    trainingSequence[i] = random(0, 4);
  }
  screen = Screen::TrainingCue;
  trainingCueUntilMs = millis() + kTrainingCueMs;
  drawTrainingScreen(directionName(trainingSequence[0]),
                     arcadeText("ZAPAMIETAJ KIERUNEK", "REMEMBER THE DIRECTION"),
                     kDirectionMarks[trainingSequence[0]], trainingStep,
                     trainingRound + 1);
}

void showNextTrainingStep() {
  const uint8_t sequenceLength = trainingRound + 1;
  if (trainingStep >= sequenceLength) {
    PetState& pet = saveData.pets[selectedPet];
    const uint32_t reward = trainingRound * 2;
    addCoins(reward);
    pet.happiness = min<uint8_t>(100, pet.happiness + 8);
    pet.energy = pet.energy > 4 ? pet.energy - 4 : 0;
    ++pet.careCount;
    dirty = true;

    if (trainingRound >= kTrainingRounds) {
      addCoins(6);
      trainingBestRound = max(trainingBestRound, trainingRound);
      saveTrainingRecord();
      screen = Screen::TrainingResult;
      display.fillScreen(display.color565(11, 17, 39));
      display.setTextColor(ST77XX_GREEN);
      display.setTextSize(2);
      display.setCursor(43, 92);
      display.print(arcadeText("TRENING UKONCZONY", "TRAINING COMPLETE"));
      display.setTextColor(ST77XX_YELLOW);
      display.setCursor(55, 144);
      display.print(arcadeText("+12 MONET", "+12 COINS"));
      display.setTextSize(1);
      display.setTextColor(ST77XX_WHITE);
      display.setCursor(40, 194);
      display.print(arcadeText("REKORD: RUNDA ", "BEST ROUND: "));
      display.print(trainingBestRound);
      display.setCursor(48, 248);
      display.print(arcadeText("JOY/A: POWROT DO PUPILA", "JOY/A: BACK TO PET"));
      saveToNvs();
      saveCoins();
      arcadePlaySound(1600, 120);
      lastDrawMs = millis();
      return;
    }

    trainingBestRound = max(trainingBestRound, trainingRound);
    saveTrainingRecord();
    saveToNvs();
    saveCoins();
    ++trainingRound;
    trainingStep = 0;
    screen = Screen::TrainingResult;
    display.fillScreen(display.color565(11, 17, 39));
    display.setTextColor(ST77XX_GREEN);
    display.setTextSize(2);
    display.setCursor(58, 110);
    display.print(arcadeText("SWIETNIE!", "GREAT!"));
    display.setTextColor(ST77XX_YELLOW);
    display.setCursor(62, 160);
    display.print("+");
    display.print(reward);
    display.print(arcadeText(" MONET", " COINS"));
    display.setTextSize(1);
    display.setTextColor(ST77XX_WHITE);
    display.setCursor(44, 223);
    display.print(arcadeText("JOY/A: NASTEPNY POZIOM",
                             "JOY/A: NEXT ROUND"));
    lastDrawMs = millis();
    arcadePlaySound(1450, 70);
    return;
  }

  screen = Screen::TrainingCue;
  trainingCueUntilMs = millis() + kTrainingCueMs;
  drawTrainingScreen(directionName(trainingSequence[trainingStep]),
                     arcadeText("ZAPAMIETAJ KIERUNEK", "REMEMBER THE DIRECTION"),
                     kDirectionMarks[trainingSequence[trainingStep]],
                     trainingStep, sequenceLength);
}

void finishTraining() {
  PetState& pet = saveData.pets[selectedPet];
  pet.energy = pet.energy > 2 ? pet.energy - 2 : 0;
  dirty = true;
  trainingBestRound = max<uint8_t>(
      trainingBestRound, trainingRound > 0 ? trainingRound - 1 : 0);
  saveToNvs();
  saveTrainingRecord();
  display.fillScreen(display.color565(35, 13, 24));
  display.setTextColor(ST77XX_ORANGE);
  display.setTextSize(2);
  display.setCursor(68, 112);
  display.print(arcadeText("PRAWIE!", "ALMOST!"));
  display.setTextColor(ST77XX_WHITE);
  display.setTextSize(1);
  display.setCursor(50, 158);
  display.print(arcadeText("UKONCZONE RUNDY: ", "ROUNDS COMPLETED: "));
  display.print(trainingRound - 1);
  display.setCursor(48, 218);
  display.print(arcadeText("JOY/A: POWROT DO PUPILA", "JOY/A: BACK TO PET"));
  lastDrawMs = millis();
  arcadePlaySound(390, 80);
  screen = Screen::TrainingResult;
}

void handleTrainingInput(int8_t direction) {
  if (screen != Screen::TrainingInput) return;
  if (direction < 0) return;
  if (static_cast<uint8_t>(direction) != trainingSequence[trainingStep]) {
    finishTraining();
    return;
  }
  ++trainingStep;
  if (trainingStep >= trainingRound + 1) {
    showNextTrainingStep();
    return;
  }
  screen = Screen::TrainingCue;
  trainingCueUntilMs = millis() + kTrainingCueMs;
  drawTrainingScreen(directionName(trainingSequence[trainingStep]),
                     arcadeText("ZAPAMIETAJ KIERUNEK", "REMEMBER THE DIRECTION"),
                     kDirectionMarks[trainingSequence[trainingStep]],
                     trainingStep, trainingRound + 1);
}

void buyShopItem() {
  const uint8_t cost = kShopCosts[selectedShopItem];
  if (coins < cost) {
    display.setTextColor(ST77XX_RED);
    display.setCursor(61, 265);
    display.print(arcadeText("ZA MALO MONET", "NOT ENOUGH COINS"));
    arcadePlaySound(300, 60);
    return;
  }

  coins -= cost;
  PetState& pet = saveData.pets[selectedPet];
  switch (selectedShopItem) {
    case 0:
      pet.hunger = min<uint8_t>(100, pet.hunger + 50);
      break;
    case 1:
      pet.happiness = min<uint8_t>(100, pet.happiness + 45);
      break;
    case 2:
      pet.health = min<uint8_t>(100, pet.health + 40);
      break;
  }
  ++pet.careCount;
  dirty = true;
  saveToNvs();
  saveCoins();
  arcadePlaySound(1500, 60);
  drawShop();
  display.setTextColor(ST77XX_GREEN);
  display.setCursor(68, 265);
  display.print(arcadeText("ZAKUP UDANY!", "PURCHASE COMPLETE!"));
}

void performAction() {
  PetState& pet = saveData.pets[selectedPet];
  if (selectedAction == static_cast<uint8_t>(Action::Train)) {
    startTraining();
    return;
  }
  if (selectedAction == static_cast<uint8_t>(Action::Shop)) {
    selectedShopItem = 0;
    screen = Screen::Shop;
    drawShop();
    return;
  }
  switch (static_cast<Action>(selectedAction)) {
    case Action::Feed:
      pet.hunger = pet.hunger > 70 ? 100 : pet.hunger + 30;
      arcadePlaySound(1000, 40);
      break;
    case Action::Play:
      pet.happiness = pet.happiness > 70 ? 100 : pet.happiness + 30;
      if (pet.energy > 10) pet.energy -= 10;
      arcadePlaySound(1350, 45);
      break;
    case Action::Clean:
      pet.cleanliness = pet.cleanliness > 65 ? 100 : pet.cleanliness + 35;
      arcadePlaySound(850, 35);
      break;
    case Action::Rest:
      pet.energy = pet.energy > 65 ? 100 : pet.energy + 35;
      arcadePlaySound(650, 50);
      break;
    case Action::Treat:
      pet.health = pet.health > 70 ? 100 : pet.health + 30;
      pet.hunger = pet.hunger > 5 ? pet.hunger - 5 : 0;
      arcadePlaySound(1500, 50);
      break;
  }
  ++pet.careCount;
  dirty = true;
  saveToNvs();
  drawScreen();
}

}  // namespace

void initialize() {
  storageReady = preferences.begin("arcade-pets", false);
  if (!storageReady) {
    Serial.println("Tamagotchi NVS unavailable; pet state will be volatile.");
    initializeDefaults();
  } else {
    coins = preferences.getUInt("coins", 0);
    trainingBestRound = preferences.getUChar("train_best", 0);
    const size_t storedLength = preferences.getBytesLength("state");
    if (storedLength == sizeof(saveData)) {
      const size_t read = preferences.getBytes("state", &saveData, sizeof(saveData));
      if (read != sizeof(saveData) || !validSaveData(saveData)) {
        Serial.println("Tamagotchi save is invalid; creating new pets.");
        initializeDefaults();
      } else {
        dirty = false;
      }
    } else {
      if (storedLength != 0) {
        Serial.printf("Tamagotchi save has unexpected size: %u bytes.\n",
                      static_cast<unsigned>(storedLength));
      }
      initializeDefaults();
    }
    if (dirty) saveToNvs();
  }
  lastServiceMs = millis();
  lastSaveMs = lastServiceMs;
}

void service() {
  const uint32_t now = millis();
  const uint32_t elapsedMs = now - lastServiceMs;
  lastServiceMs = now;
  const uint64_t accumulatedMs =
      static_cast<uint64_t>(elapsedRemainderMs) + elapsedMs;
  const uint32_t elapsedSeconds =
      static_cast<uint32_t>(accumulatedMs / 1000);
  elapsedRemainderMs = static_cast<uint32_t>(accumulatedMs % 1000);

  if (elapsedSeconds > 0) {
    for (PetState& pet : saveData.pets) applyElapsed(pet, elapsedSeconds);
    dirty = true;
  }
  if (dirty && now - lastSaveMs >= kSaveIntervalMs) saveToNvs();
}

void begin() {
  selectedPet = 0;
  selectedAction = 0;
  screen = Screen::Pet;
  display.init(240, 320);
  display.setRotation(0);
  display.invertDisplay(arcadeDisplayInverted());

  pinMode(pins::joy_left, INPUT_PULLUP);
  pinMode(pins::joy_right, INPUT_PULLUP);
  pinMode(pins::joy_up, INPUT_PULLUP);
  pinMode(pins::joy_down, INPUT_PULLUP);
  pinMode(pins::joy_button, INPUT_PULLUP);
  pinMode(pins::button_p1, INPUT_PULLUP);
  pinMode(pins::button_p2, INPUT_PULLUP);
  pinMode(pins::button_p3, INPUT_PULLUP);
  pinMode(pins::button_p4, INPUT_PULLUP);

  previousLeft = pressed(pins::joy_left);
  previousRight = pressed(pins::joy_right);
  previousUp = pressed(pins::joy_up);
  previousDown = pressed(pins::joy_down);
  previousJoyButton = pressed(pins::joy_button);
  previousP1 = pressed(pins::button_p1);
  previousP2 = pressed(pins::button_p2);
  previousP3 = pressed(pins::button_p3);
  previousP4 = pressed(pins::button_p4);
  drawScreen();
}

void tick() {
  service();
  const uint32_t now = millis();

  const bool p4Edge = pressedEdge(pins::button_p4, previousP4);
  const bool p3Edge = pressedEdge(pins::button_p3, previousP3);
  const bool leftEdge = pressedEdge(pins::joy_left, previousLeft);
  const bool rightEdge = pressedEdge(pins::joy_right, previousRight);
  const bool upEdge = pressedEdge(pins::joy_up, previousUp);
  const bool downEdge = pressedEdge(pins::joy_down, previousDown);
  const bool joyButtonEdge = pressedEdge(pins::joy_button, previousJoyButton);
  const bool p1Edge = pressedEdge(pins::button_p1, previousP1);
  const bool p2Edge = pressedEdge(pins::button_p2, previousP2);

  if (p4Edge) {
    if (dirty && storageReady) saveToNvs();
    arcadeRequestGameExit();
    return;
  }
  if (p3Edge) {
    arcadeToggleDisplayInversion();
    display.invertDisplay(arcadeDisplayInverted());
    if (screen == Screen::Pet) drawScreen();
    else if (screen == Screen::Shop) drawShop();
  } else if (leftEdge) {
    if (screen == Screen::Shop) {
      selectedShopItem = (selectedShopItem + 2) % 3;
      drawShop();
    } else if (screen == Screen::Pet) {
      selectedPet = (selectedPet + kPetCount - 1) % kPetCount;
      drawScreen();
    }
  } else if (rightEdge) {
    if (screen == Screen::Shop) {
      selectedShopItem = (selectedShopItem + 1) % 3;
      drawShop();
    } else if (screen == Screen::Pet) {
      selectedPet = (selectedPet + 1) % kPetCount;
      drawScreen();
    }
  } else if (upEdge) {
    if (screen == Screen::Shop) {
      selectedShopItem = (selectedShopItem + 2) % 3;
      drawShop();
    } else if (screen == Screen::Pet) {
      selectedAction = (selectedAction + kActionCount - 1) % kActionCount;
      drawScreen();
    }
  } else if (downEdge) {
    if (screen == Screen::Shop) {
      selectedShopItem = (selectedShopItem + 1) % 3;
      drawShop();
    } else if (screen == Screen::Pet) {
      selectedAction = (selectedAction + 1) % kActionCount;
      drawScreen();
    }
  } else if (joyButtonEdge || p1Edge) {
    if (screen == Screen::Pet) performAction();
    else if (screen == Screen::Shop) buyShopItem();
    else if (screen == Screen::TrainingResult) {
      if (trainingRound >= kTrainingRounds) {
        screen = Screen::Pet;
        drawScreen();
      } else {
        screen = Screen::TrainingCue;
        trainingStep = 0;
        trainingCueUntilMs = now + kTrainingCueMs;
        drawTrainingScreen(directionName(trainingSequence[0]),
                           arcadeText("ZAPAMIETAJ KIERUNEK",
                                      "REMEMBER THE DIRECTION"),
                           kDirectionMarks[trainingSequence[0]], 0,
                           trainingRound + 1);
      }
    }
  } else if (p2Edge) {
    if (screen == Screen::Pet) {
      selectedAction = (selectedAction + 1) % kActionCount;
      drawScreen();
    } else if (screen == Screen::Shop) {
      screen = Screen::Pet;
      drawScreen();
    }
  }

  if (screen == Screen::TrainingCue &&
      static_cast<int32_t>(now - trainingCueUntilMs) >= 0) {
    screen = Screen::TrainingInput;
    trainingDeadlineMs = now + kTrainingInputMs;
    drawTrainingScreen("?", arcadeText("POWTORZ KIERUNEK", "REPEAT THE DIRECTION"),
                       '?', trainingStep,
                       trainingRound + 1);
  } else if (screen == Screen::TrainingInput) {
    if (static_cast<int32_t>(now - trainingDeadlineMs) >= 0) {
      finishTraining();
    } else {
      const int8_t direction = upEdge ? 0 : rightEdge ? 1 :
                               downEdge ? 2 : leftEdge ? 3 : -1;
      handleTrainingInput(direction);
    }
  }

  if (screen == Screen::Pet && now - lastDrawMs >= 5000) drawScreen();
}

}  // namespace tamagotchi
