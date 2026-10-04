#pragma once

#include <stdint.h>

bool arcadePersistenceBegin();
uint32_t arcadeLoadUInt(const char* key, uint32_t fallback);
bool arcadeStoreUInt(const char* key, uint32_t value);
bool arcadeStoreHighScore(const char* key, uint32_t score);
