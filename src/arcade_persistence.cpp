#include "arcade_persistence.h"

#include <Arduino.h>
#include <Preferences.h>

namespace {

Preferences preferences;
bool ready = false;

}

bool arcadePersistenceBegin() {
  ready = preferences.begin("arcade", false);
  if (!ready) Serial.println("NVS error: unable to open arcade preferences.");
  return ready;
}

uint32_t arcadeLoadUInt(const char* key, uint32_t fallback) {
  if (!ready) {
    Serial.println("NVS error: arcade preferences are not initialized.");
    return fallback;
  }
  return preferences.getUInt(key, fallback);
}

bool arcadeStoreUInt(const char* key, uint32_t value) {
  if (!ready) {
    Serial.println("NVS error: arcade preferences are not initialized.");
    return false;
  }
  if (preferences.putUInt(key, value) != sizeof(value)) {
    Serial.printf("NVS error: failed to save '%s'.\n", key);
    return false;
  }
  return true;
}

bool arcadeStoreHighScore(const char* key, uint32_t score) {
  if (score <= arcadeLoadUInt(key, 0)) return true;
  return arcadeStoreUInt(key, score);
}
