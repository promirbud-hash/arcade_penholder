# Arcade Penholder

Firmware przenośnej konsoli arcade zbudowanej na ESP32-S3. Projekt łączy
launcher, gry zręcznościowe i proste aplikacje na ekranie TFT ST7789
240 × 320 px.

## Zawartość

**Gry:** Doom, Bag-Man, Snake, Tetris, Yellow Racer i Arkanoid.

**Aplikacje:** Tamagotchi z treningiem i sklepem, timer Pomodoro oraz ekran
odczytów temperatury i wilgotności z czujnika DHT22.

Interfejs i gry (poza Doomem) obsługują język polski i angielski. Ustawienia
języka, globalnego wyciszenia i poziomu głośności są zapisywane w pamięci NVS.
Rekordy gier i dane aplikacji również są przechowywane lokalnie. Podświetleniem
TFT nie można sterować programowo przy obecnym pinoucie, ponieważ nie
przypisano do niego pinu GPIO.

## Sprzęt i pinout

Projekt jest skonfigurowany dla ESP32-S3 N16R8 (16 MB Flash i 8 MB PSRAM).
Wymaga wykrywania PSRAM przez firmware.

| Element | Sygnał | GPIO |
| --- | --- | ---: |
| TFT ST7789 | CS | 14 |
|  | DC | 13 |
|  | RST | 12 |
|  | MOSI | 11 |
|  | CLK | 10 |
|  | MISO | nieużywany |
| Joystick | góra / dół | 17 / 18 |
|  | lewo / prawo | 3 / 8 |
|  | przycisk | 16 |
| Przyciski | A / B / X / Z | 15 / 7 / 6 / 5 |
| Audio | wyjście dźwięku | 4 |
| Czujnik DHT22 | DATA | 9 |

Wejścia joysticka i przycisków są aktywne stanem niskim i używają
wewnętrznych rezystorów podciągających. W menu głównym i menu gier przycisk B
otwiera ustawienia.

## Budowanie i wgrywanie

Zainstaluj PlatformIO Core i uruchom polecenia w katalogu repozytorium:

```powershell
py -m platformio run -e esp32-s3-devkitc-1-n16r8
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t upload
```

Firmware używa LittleFS do plików Dooma. Po dodaniu lub zmianie plików w `data`
zbuduj i wgraj obraz systemu plików:

```powershell
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t buildfs
py -m platformio run -e esp32-s3-devkitc-1-n16r8 -t uploadfs
```

`data/doom1.wad` nie jest dołączany do repozytorium. Aby uruchomić Dooma,
pozyskaj zgodny plik WAD legalnie, umieść go jako `data/doom1.wad`, a następnie
zbuduj i wgraj LittleFS. Nie commituj pliku WAD.

## Dokumentacja i licencje

Szczegółowe informacje o konfiguracji, audio, sterowaniu i systemie plików
znajdują się w [DOOM_SETUP.md](./DOOM_SETUP.md). Kod silnika DoomGeneric jest
dostarczony w `src/doomgeneric`; jego licencja GPL-2.0 znajduje się w
`src/doomgeneric/LICENSE`. Adaptacja Yellow Racer zawiera informację o licencji
MIT w `LICENSE-Yellow-Racer.txt`. Sprawdź licencje poszczególnych komponentów
przed ich dalszą dystrybucją.
