#pragma once

#include <stdint.h>

class Adafruit_ST7789;

bool arcadeDisplayInverted();
Adafruit_ST7789& arcadeSharedDisplay();
void arcadeToggleDisplayInversion();
void arcadeRequestGameExit();
bool arcadeSoundEnabled();
void arcadeSetSoundEnabled(bool enabled);
void arcadeSetVolume(uint8_t level);
uint8_t arcadeVolume();
bool arcadeIsPolish();
const char* arcadeText(const char* polish, const char* english);
void arcadePlaySound(uint16_t frequency, uint16_t durationMs);
void arcadeStopSound();
void arcadeSoundService();
