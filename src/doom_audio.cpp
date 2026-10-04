#include "i_sound.h"
#ifdef __cplusplus
extern "C" {
#endif
#include "w_wad.h"
#include "z_zone.h"
#ifdef __cplusplus
}
#endif
#include "pins.h"
#include "doom_audio_runtime.h"
#include "arcade_runtime.h"

#include <esp_timer.h>
#include <driver/i2s.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include <stdio.h>
#include <string.h>

int use_libsamplerate = 0;
float libsamplerate_scale = 0.65f;

namespace {

constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr uint32_t kOutputSampleRate = 22050;
constexpr size_t kFramesPerBuffer = 256;
constexpr uint8_t kChannelCount = 16;

enum class CommandType : uint8_t { Start, Stop };

struct AudioCommand {
  CommandType type;
  uint8_t channel;
  uint8_t volume;
  const uint8_t* samples;
  uint32_t length;
  uint32_t sampleRate;
};

struct Voice {
  const uint8_t* samples = nullptr;
  uint32_t length = 0;
  uint32_t sampleRate = 0;
  uint32_t position = 0;
  uint32_t phase = 0;
  uint8_t volume = 0;
};

snddevice_t soundDevices[] = {SNDDEVICE_SB, SNDDEVICE_PCSPEAKER};
QueueHandle_t audioQueue = nullptr;
SemaphoreHandle_t audioStopped = nullptr;
TaskHandle_t audioTaskHandle = nullptr;
uint32_t channelEnds[kChannelCount] = {};
bool audioStarted = false;
volatile bool audioTaskRunning = false;
volatile bool audioStopRequested = false;
bool useSfxPrefix = true;

uint32_t currentTimeMs() {
  return static_cast<uint32_t>(esp_timer_get_time() / 1000);
}

uint32_t readLittleEndian32(const uint8_t* data) {
  return static_cast<uint32_t>(data[0]) |
         (static_cast<uint32_t>(data[1]) << 8) |
         (static_cast<uint32_t>(data[2]) << 16) |
         (static_cast<uint32_t>(data[3]) << 24);
}

void audioTask(void*) {
  Voice voices[kChannelCount];
  int16_t output[kFramesPerBuffer];

  while (!audioStopRequested) {
    AudioCommand command;
    while (xQueueReceive(audioQueue, &command, 0) == pdTRUE) {
      if (command.channel < kChannelCount) {
        Voice& voice = voices[command.channel];
        if (command.type == CommandType::Stop) {
          voice = Voice{};
        } else {
          voice.samples = command.samples;
          voice.length = command.length;
          voice.sampleRate = command.sampleRate;
          voice.position = 0;
          voice.phase = 0;
          voice.volume = command.volume;
        }
      }
    }

    for (size_t frame = 0; frame < kFramesPerBuffer; ++frame) {
      int32_t mixedSample = 0;
      for (uint8_t channel = 0; channel < kChannelCount; ++channel) {
        Voice& voice = voices[channel];
        if (voice.samples == nullptr || voice.position >= voice.length) continue;

        const int32_t sample = static_cast<int32_t>(voice.samples[voice.position]) - 128;
        mixedSample += sample * voice.volume * 120 / 127;

        voice.phase += voice.sampleRate;
        while (voice.phase >= kOutputSampleRate) {
          voice.phase -= kOutputSampleRate;
          if (++voice.position >= voice.length) {
            voice = Voice{};
            break;
          }
        }
      }

      if (mixedSample > INT16_MAX) mixedSample = INT16_MAX;
      if (mixedSample < INT16_MIN) mixedSample = INT16_MIN;
      output[frame] = arcadeSoundEnabled()
                          ? static_cast<int16_t>(mixedSample)
                          : 0;
    }

    size_t bytesWritten = 0;
    const esp_err_t result = i2s_write(
        kI2sPort, output, sizeof(output), &bytesWritten, pdMS_TO_TICKS(20));
    if (result != ESP_OK || bytesWritten != sizeof(output)) {
      if (!audioStopRequested) {
        printf("Doom audio: I2S write failed (%s, %u/%u bytes).\n",
               esp_err_to_name(result),
               static_cast<unsigned>(bytesWritten),
               static_cast<unsigned>(sizeof(output)));
      }
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }

  audioTaskRunning = false;
  xSemaphoreGive(audioStopped);
  vTaskDelete(nullptr);
}

boolean soundInit(boolean sfxPrefix) {
  useSfxPrefix = sfxPrefix;
  return arcadeDoomAudioStart();
}

void soundShutdown() {
  arcadeDoomAudioStop();
}

int getSoundLump(sfxinfo_t* sound) {
  if (sound == nullptr) return -1;
  if (sound->link != nullptr) sound = sound->link;

  char lumpName[9] = {};
  if (useSfxPrefix) {
    lumpName[0] = 'd';
    lumpName[1] = 's';
    strncpy(lumpName + 2, sound->name, sizeof(lumpName) - 3);
  } else {
    strncpy(lumpName, sound->name, sizeof(lumpName) - 1);
  }
  const int lump = W_CheckNumForName(lumpName);
  static uint8_t lookupLogCount = 0;
  if (lookupLogCount < 8) {
    printf("Doom audio: lump %s -> %d\n", lumpName, lump);
    ++lookupLogCount;
  }
  return lump;
}

int startSound(sfxinfo_t* sound, int channel, int volume, int) {
  if (!audioStarted || audioQueue == nullptr || sound == nullptr ||
      channel < 0 || channel >= kChannelCount || sound->lumpnum < 0) {
    return -1;
  }

  const uint32_t now = currentTimeMs();
  uint8_t* lump = static_cast<uint8_t*>(
      W_CacheLumpNum(sound->lumpnum, PU_STATIC));
  const int lumpLength = W_LumpLength(sound->lumpnum);
  if (lump == nullptr || lumpLength < 56 ||
      lump[0] != 0x03 || lump[1] != 0x00) {
    static uint8_t invalidLogCount = 0;
    if (invalidLogCount < 8) {
      printf("Doom audio: invalid sample lump=%d size=%d.\n",
             sound->lumpnum, lumpLength);
      ++invalidLogCount;
    }
    return -1;
  }

  const uint32_t sampleRate =
      static_cast<uint32_t>(lump[2]) |
      (static_cast<uint32_t>(lump[3]) << 8);
  const uint32_t declaredLength = readLittleEndian32(lump + 4);
  if (sampleRate < 4000 || sampleRate > 48000 ||
      declaredLength > static_cast<uint32_t>(lumpLength - 8) ||
      declaredLength <= 48 || declaredLength <= 32) {
    static uint8_t headerLogCount = 0;
    if (headerLogCount < 8) {
      printf("Doom audio: bad sample header lump=%d rate=%lu size=%lu/%d.\n",
             sound->lumpnum, static_cast<unsigned long>(sampleRate),
             static_cast<unsigned long>(declaredLength), lumpLength);
      ++headerLogCount;
    }
    return -1;
  }

  const uint32_t sampleLength = declaredLength - 32;
  const uint8_t* samples = lump + 24;
  if (sampleLength > static_cast<uint32_t>(lumpLength - 24)) return -1;

  AudioCommand command{
      CommandType::Start,
      static_cast<uint8_t>(channel),
      static_cast<uint8_t>(volume < 0 ? 0 : (volume > 127 ? 127 : volume)),
      samples,
      sampleLength,
      sampleRate,
  };
  if (xQueueSend(audioQueue, &command, 0) != pdTRUE) {
    printf("Doom audio: playback queue full; sound effect dropped.\n");
    return -1;
  }

  const uint32_t durationMs = sampleLength * 1000UL / sampleRate;
  static uint8_t playbackLogCount = 0;
  if (playbackLogCount < 8) {
    printf("Doom audio: playing lump=%d, %lu Hz, %lu samples, volume=%u.\n",
           sound->lumpnum, static_cast<unsigned long>(sampleRate),
           static_cast<unsigned long>(sampleLength),
           static_cast<unsigned>(command.volume));
    ++playbackLogCount;
  }
  channelEnds[channel] = now + durationMs;
  return channel;
}

void updateSoundParams(int, int, int) {}

void updateSound() {}

void stopSound(int channel) {
  if (channel < 0 || channel >= kChannelCount || audioQueue == nullptr) return;
  channelEnds[channel] = 0;
  const AudioCommand command{CommandType::Stop, static_cast<uint8_t>(channel),
                             0, nullptr, 0, 0};
  xQueueSend(audioQueue, &command, 0);
}

boolean soundIsPlaying(int channel) {
  return channel >= 0 && channel < kChannelCount &&
         static_cast<int32_t>(channelEnds[channel] - currentTimeMs()) > 0;
}

void cacheSounds(sfxinfo_t*, int) {}

boolean musicInit() { return true; }
void musicShutdown() {}
void setMusicVolume(int) {}
void pauseMusic() {}
void resumeMusic() {}
void* registerSong(void*, int) { return nullptr; }
void unregisterSong(void*) {}
void playSong(void*, boolean) {}
void stopSong() {}
boolean musicIsPlaying() { return false; }
void pollMusic() {}

void finishAudioStop() {
  const esp_err_t result = i2s_driver_uninstall(kI2sPort);
  if (result != ESP_OK) {
    printf("Doom audio: I2S shutdown failed: %s\n", esp_err_to_name(result));
  }
  vQueueDelete(audioQueue);
  audioQueue = nullptr;
  vSemaphoreDelete(audioStopped);
  audioStopped = nullptr;
  audioTaskHandle = nullptr;
  audioStarted = false;
  audioTaskRunning = false;
}

}  // namespace

bool arcadeDoomAudioStart() {
  if (audioStarted) {
    if (audioTaskRunning) return true;
    if (audioStopped == nullptr ||
        xSemaphoreTake(audioStopped, 0) != pdTRUE) {
      printf("Doom audio: waiting for previous playback task to stop.\n");
      return false;
    }
    finishAudioStop();
  }

  const i2s_config_t config = {
      .mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX | I2S_MODE_PDM),
      .sample_rate = kOutputSampleRate,
      .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
      .communication_format = I2S_COMM_FORMAT_STAND_I2S,
      .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
      .dma_buf_count = 6,
      .dma_buf_len = kFramesPerBuffer,
      .use_apll = false,
      .tx_desc_auto_clear = true,
      .fixed_mclk = 0,
  };
  esp_err_t result = i2s_driver_install(kI2sPort, &config, 0, nullptr);
  if (result != ESP_OK) {
    printf("Doom audio: I2S driver init failed: %s\n", esp_err_to_name(result));
    return false;
  }
  result = i2s_set_clk(kI2sPort, kOutputSampleRate, I2S_BITS_PER_SAMPLE_16BIT,
                       I2S_CHANNEL_MONO);
  if (result != ESP_OK) {
    printf("Doom audio: I2S mono clock setup failed: %s\n", esp_err_to_name(result));
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  const i2s_pin_config_t pinConfig = {
      .mck_io_num = I2S_PIN_NO_CHANGE,
      .bck_io_num = I2S_PIN_NO_CHANGE,
      .ws_io_num = I2S_PIN_NO_CHANGE,
      .data_out_num = pins::audio,
      .data_in_num = I2S_PIN_NO_CHANGE,
  };
  result = i2s_set_pin(kI2sPort, &pinConfig);
  if (result != ESP_OK) {
    printf("Doom audio: I2S GPIO setup failed: %s\n", esp_err_to_name(result));
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  audioQueue = xQueueCreate(32, sizeof(AudioCommand));
  if (audioQueue == nullptr) {
    printf("Doom audio: unable to create playback queue.\n");
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  audioStopped = xSemaphoreCreateBinary();
  if (audioStopped == nullptr) {
    printf("Doom audio: unable to create task completion semaphore.\n");
    vQueueDelete(audioQueue);
    audioQueue = nullptr;
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  audioStopRequested = false;
  audioTaskRunning = true;
  if (xTaskCreatePinnedToCore(audioTask, "doom-audio", 4096, nullptr, 3,
                              &audioTaskHandle, 0) != pdPASS) {
    audioTaskRunning = false;
    printf("Doom audio: unable to create playback task.\n");
    vSemaphoreDelete(audioStopped);
    audioStopped = nullptr;
    vQueueDelete(audioQueue);
    audioQueue = nullptr;
    i2s_driver_uninstall(kI2sPort);
    return false;
  }

  memset(channelEnds, 0, sizeof(channelEnds));
  audioStarted = true;
  printf("Doom audio: WAD samples enabled on GPIO4 via I2S PDM.\n");
  return true;
}

void arcadeDoomAudioStop() {
  if (!audioStarted) return;

  audioStopRequested = true;
  if (xSemaphoreTake(audioStopped, pdMS_TO_TICKS(250)) == pdTRUE) {
    finishAudioStop();
    memset(channelEnds, 0, sizeof(channelEnds));
  } else {
    printf("Doom audio: shutdown deferred; returning to menu without waiting.\n");
  }
}

sound_module_t DG_sound_module = {
    soundDevices,
    static_cast<int>(sizeof(soundDevices) / sizeof(soundDevices[0])),
    soundInit,
    soundShutdown,
    getSoundLump,
    updateSound,
    updateSoundParams,
    startSound,
    stopSound,
    soundIsPlaying,
    cacheSounds,
};

music_module_t DG_music_module = {
    soundDevices,
    static_cast<int>(sizeof(soundDevices) / sizeof(soundDevices[0])),
    musicInit,
    musicShutdown,
    setMusicVolume,
    pauseMusic,
    resumeMusic,
    registerSong,
    unregisterSong,
    playSong,
    stopSong,
    musicIsPlaying,
    pollMusic,
};
