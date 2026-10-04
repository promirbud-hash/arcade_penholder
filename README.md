# Arcade Penholder

Firmware for a portable arcade console built around an ESP32-S3. The project
brings together a launcher, arcade games, and simple applications on a
240 × 320 px ST7789 TFT display.

## What's included

**Games:** Doom, Bag-Man, Snake, Tetris, Yellow Racer, and Arkanoid.

**Applications:** Tamagotchi with training and a shop, a Pomodoro timer, and a
readout screen for temperature and humidity from a DHT22 sensor.

The interface, games, and applications support Polish and English; Doom is
not translated. Language, global mute, and volume settings are saved in NVS.
Game high scores and application data are also stored locally. TFT backlight
brightness cannot be controlled with the current pinout because no GPIO is
assigned to the backlight.

## Hardware and pinout

The project is configured for an ESP32-S3 N16R8 (16 MB Flash and 8 MB PSRAM).
The firmware requires PSRAM to be detected at startup.

| Component | Signal | GPIO |
| --- | --- | ---: |
| ST7789 TFT | CS | 14 |
|  | DC | 13 |
|  | RST | 12 |
|  | MOSI | 11 |
|  | CLK | 10 |
|  | MISO | unused |
| Joystick | up / down | 17 / 18 |
|  | left / right | 3 / 8 |
|  | button | 16 |
| Buttons | A / B / X / Z | 15 / 7 / 6 / 5 |
| Audio | sound output | 4 |
| DHT22 sensor | DATA | 9 |

Joystick and button inputs are active-low and use internal pull-up resistors.
Press B in the main or games menu to open Settings.

## Build and upload

Install PlatformIO Core and run these commands from the repository directory:

```powershell
py -m platformio run -e esp32-s3-devkitc-1-n16r8
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t upload
```

The firmware uses LittleFS for Doom files. After adding or changing files in
`data`, build and upload the filesystem image:

```powershell
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t buildfs
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t uploadfs
```

`data/doom1.wad` is not included in this repository. To play Doom, obtain a
compatible WAD file legally, place it at `data/doom1.wad`, then build and
upload LittleFS. Do not commit the WAD file.

## Documentation and licenses

See [DOOM_SETUP.md](./DOOM_SETUP.md) for detailed setup, audio, controls, and
filesystem information. The DoomGeneric engine source is included in
`src/doomgeneric`; its GPL-2.0 license is in `src/doomgeneric/LICENSE`. The
Yellow Racer adaptation includes its MIT license in
`LICENSE-Yellow-Racer.txt`. Check the licenses of individual components
before redistributing them.
