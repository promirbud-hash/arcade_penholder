# Doom on ESP32-S3

The project vendors the DoomGeneric engine from `ozkl/doomgeneric` under
`src/doomgeneric`; its upstream GPL-2.0 license is retained in
`src/doomgeneric/LICENSE`. PlatformIO compiles the generic engine sources and
omits the desktop-specific backends.

## Launcher, display, and controls

On boot the firmware starts in the portrait-oriented arcade launcher. The main
menu uses larger, bold centered labels and an animated coin selector. It
contains Games, Tamagotchi, Pomodoro, Screensaver, and Sleep. Tamagotchi
includes five persistent characters and care actions, with age and needs
advanced by powered-on time. Pomodoro provides a focus/break timer with
adjustable work and break lengths, sound, and cycle target. The screensaver
displays DHT22 temperature and humidity readings from GPIO 9
(connect DHT DATA to GPIO9 and share ground with the ESP32) and provides five
font choices. Sleep blanks the screen while leaving the ESP32 running and
polling buttons normally; any joystick direction/button or A/B/X/Z restores the
launcher. The current pinout does not define a controllable TFT backlight pin,
so this blanks the display image but cannot switch off the backlight itself.
The Games submenu contains Doom, Bag-Man, Snake, Tetris, Yellow Racer, and
Arkanoid. Yellow Racer uses the joystick to choose a track and steer;
traffic cars appear during races, with more cars on the harder tracks. Earn one
point for each car passed; hitting a car ends the run. Press A to retry the
same track after a crash, or press the joystick to return to the track menu.
Arkanoid uses the joystick left/right to move the paddle, A to launch, and B
to pause.

Press B in the launcher or game menu to open Settings. There you can toggle
sound, select one of four sound levels, and switch between Polish and English;
these choices are saved in NVS. Language changes apply to the launcher,
non-Doom games, and applications. Volume changes apply to the GPIO tone output;
Doom's WAD audio keeps its own playback level, but follows the global mute.
Backlight adjustment is unavailable with the current pinout because no TFT
backlight control GPIO is connected or defined.
The launcher, non-Doom games, and Doom share one Adafruit_ST7789 instance on
the pin assignments in `include/pins.h` (CS 14, DC 13, reset 12, MOSI 11,
clock 10). This avoids switching SPI hosts and display libraries while the
same panel is active. Doom renders at 320x200, then scales the framebuffer to
240x220 centered on the 240x320 portrait display, leaving approximately
50-pixel black bars at the top and bottom. This mildly stretches the original
aspect ratio. The framebuffer is converted to native RGB565 before transfer.

The 320x200 Doom framebuffer and the 6 MiB Doom zone are allocated from PSRAM;
the RGB565 transfer buffer also prefers PSRAM. This port therefore depends on
the N16R8 module's 8 MiB PSRAM being detected at boot.

Controls are active-low with internal pull-ups. The launcher uses the
joystick's up/down directions to move and the joystick button to select; Z
returns from a submenu/page. A/B/X/Z correspond to the physical P1/P2/P3/P4
buttons. B toggles menu navigation sounds, with the setting saved in NVS.
X is the shared display-inversion toggle. In games, Z returns to the launcher.
Doom uses:

| Input | Doom key |
| --- | --- |
| Joystick up/down/left/right | Arrow keys |
| Joystick button | Confirm / start game (Enter) |
| A (P1) | Fire (Control) |
| B (P2) | Use (Space) |
| X (P3) | Invert display |
| Z (P4) | Return to launcher |

Bag-Man uses A for pause and B for turbo. Snake uses B for pause. Tetris
uses A/B to rotate. Arkanoid keeps a persistent high score in NVS. X inverts
the display in each game.

Pomodoro uses B to start or pause, A to enter/save settings, and Z to return
to the launcher from its timer screen. In settings, joystick up/down selects
an option and X/Z increases/decreases its value. The last option resets the
Pomodoro counters after confirmation with A; Z cancels. This clears total
focus time, completed session counts, and cycle progress, but keeps timer
settings. Pomodoro statistics and settings are stored in NVS and checkpointed
every five minutes, on interval completion, and when leaving the app.
X inverts the display on the timer screen.

The screensaver uses joystick left/right to change the readout font and X to
toggle only the background between white and black. A, B, Z, the joystick
button, and joystick up/down return to the launcher.

Tamagotchi uses joystick left/right to select a character and up/down to
select a care action. Press the joystick button or A to perform it; B also
advances the action selection. Training presents a three-round sequence
memory challenge using joystick directions; correct rounds earn coins and
raise happiness, while mistakes cost a little energy. The shop exchanges
coins for food, a toy, or medicine. Coins and the best training round are
saved in NVS separately from the existing pet save. X inverts the display
and Z returns to the launcher. Character state is checkpointed to NVS every
five minutes and when leaving the Tamagotchi screen. Without an RTC, elapsed
time while the console is powered off cannot be measured; growth is based on
powered-on uptime.

## WAD and filesystem

The supplied `data/doom1.wad` is the shareware IWAD and is not modified by the
firmware. LittleFS is mounted without formatting on failure. A missing or
invalid WAD only prevents Doom from starting; the launcher remains usable.
Doom save slots and configuration are stored under `/littlefs/doom/.savegame`
and `/littlefs/doom`. Bag-Man, Snake, Tetris, and Yellow Racer records plus
Pomodoro statistics/settings are stored in NVS. Yellow Racer keeps one best
score per difficulty. Filesystem re-uploading can erase Doom save slots;
firmware-only uploads do not intentionally format LittleFS.

Build and upload the filesystem image, then flash the firmware:

```powershell
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t buildfs
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t uploadfs
py -m platformio run -e esp32-s3-devkitc-1-n16r8
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t upload
```

## Audio

Music remains disabled with `-nomusic`. Doom decodes the original 8-bit sound
effect samples from the WAD and mixes them through I2S PDM on GPIO 4; it does
not replace them with synthesized beeps. Bag-Man, Snake, Tetris, Yellow Racer,
Arkanoid, and the other applications continue to use short GPIO 4 tone
feedback.
The PDM output needs the previously discussed RC low-pass filter before the
PAM8403 input. Do not connect a speaker directly to the ESP32 pin.
