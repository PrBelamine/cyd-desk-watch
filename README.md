![Desk Watch CYD: install in 1 click, no code](docs/4par3%20Installation%20ESP32%20en%20un%20clic,%20sans%20code.png)

# Desk Watch CYD V1

A desk companion for the **ESP32-2432S028R "Cheap Yellow Display" (CYD, 2.8" touchscreen)**.
It is the touchscreen port of the *Desk Watch JOY STUDENT edition* (Arduino UNO + joystick) by Pr. BELAMINE MakerLab.

![Desk Watch running on an ESP32 CYD in a 3D-printed stand](docs/desk-watch-photo.jpg)

**Install in one click (no code):** open the install page in Chrome or Edge, plug the board in over USB and click *Install*: https://minitech.club/cyd-desk-watch/desk-watch/

**3D-printed stand:** [CYD 2.8 Display Stand - Minimalist Open Design on MakerWorld](https://makerworld.com/en/models/3513895-cyd-2-8-display-stand-minimalist-open-design) (the stand shown in the photos).

## Photos of the real device

| Clock | Timer | Stopwatch |
| --- | --- | --- |
| ![Clock](docs/photos/clock.jpg) | ![Timer](docs/photos/timer.jpg) | ![Stopwatch](docs/photos/stopwatch.jpg) |
| **Counter** | **Alarm** | **Notes (Post-it)** |
| ![Counter](docs/photos/counter.jpg) | ![Alarm](docs/photos/alarm.jpg) | ![Notes](docs/photos/notes.jpg) |

![Settings](docs/photos/settings.jpg)

## What it does

| Page | Features |
| --- | --- |
| **Clock** | Large time (synchronised over Wi-Fi), date, alarm status |
| **Timer** | Pomodoro: work 1-60 min in 1 min steps, then 65 min to 4 h in 5 min steps; 5 min breaks, 15 min long break every 4th session; keeps running on other pages |
| **Chrono** | Stopwatch with tenths of a second, start / stop / reset; keeps running on other pages |
| **Count** | Tally counter 0-9999, lock button, reset with confirmation, value kept after power loss |
| **Alarm** | Set the time with the arrows, 4 original melodies with preview, volume (5 levels + off), snooze 5 min, stops after 2 min and rings again |
| **Notes** | Digital Post-it: up to 18 tasks (3 screens of 6, use the arrows to change screen), tick them on the screen, edit them from your phone at `http://postit.local` |
| **Setup** | Brightness, volume (off + 5 levels), night mode (screen dims to 8 % and timer beeps are muted between 22:00 and 06:00), touch calibration |

Small letters at the top right show what runs in the background: **R** alarm armed, **M** timer running, **C** stopwatch running.
Swipe left/right or tap the bottom bar to change page. Hold on an empty area for 3 s (or touch the screen while it boots) to recalibrate the touch.

## Install without Arduino (web installer)

The `flasher/` folder is a ready-to-host page (ESP Web Tools): plug the board in over USB, open the page in Chrome or Edge, click **Install**, then enter your Wi-Fi name and password when the page asks.
The Wi-Fi settings travel over the USB cable (Improv Wi-Fi Serial) and are stored in the board's flash, never in the firmware file.
Host the folder on any HTTPS site (for example minitech.club). `flasher/firmware.bin` is built by `tools/build_firmware.sh`.

## Install from source

1. **Arduino IDE 2.x** with the **esp32 by Espressif** board package (3.x tested target) and these libraries (Library Manager): **TFT_eSPI** and **XPT2046_Touchscreen**.
2. Replace `Documents/Arduino/libraries/TFT_eSPI/User_Setup.h` with `User_Setup_CYD.h` from this repository (rename it to `User_Setup.h`).
   If you already use the CYD with TFT_eSPI, your existing setup works as long as it enables `SMOOTH_FONT`.
3. Either copy `Desk_Watch_CYD_V1/Secrets.example.h` to `Desk_Watch_CYD_V1/Secrets.h` and enter your Wi-Fi name and password, or skip this step and set the Wi-Fi later from the web installer page (Improv, over USB).
4. Language (English/French) and time zone (country + city, default Hanoi) are chosen after the first start from `http://postit.local/settings`; the language can also be switched on the Settings page of the board.
5. Open `Desk_Watch_CYD_V1.ino`, choose **ESP32 Dev Module**, the default partition scheme, and upload. No LittleFS data upload is needed.

## Digital Post-it

When the board is on your Wi-Fi, open `http://postit.local` (or the IP address shown on the Notes page) from a phone or computer on the same network.
Add, tick, delete tasks or clear the finished ones; the screen updates immediately, and ticking on the screen updates the page.
The page has **no password**: anyone on your local network can edit the list. Do not expose the board to the internet.
`http://postit.local/settings` lists every country and city (IANA tz database, bundled as `Desk_Watch_CYD_V1/TimeZones.h`; regenerate it with `python3 tools/make_timezones.py`). The Post-it, settings and installer pages are bilingual (English/French).

Tasks are limited to 60 characters; the font covers French and other Latin accents (characters outside it show as `?`).

## Tests

`python3 tools/test.py` (needs g++) runs the host tests of the logic: Pomodoro, stopwatch, counter, alarm and snooze, touch calibration, navigation, formatting, and the task list.
The drawing, touch, sound and web code runs only on the board.

`python3 tools/make_fonts.py` regenerates the embedded fonts (needs Pillow and DejaVu Sans Bold).

## Credits

Code and design: Pr. BELAMINE MakerLab. The Post-it idea was inspired by Mechmaestro's Digital Post-it (no code reused).
See `THIRD_PARTY_NOTICES.txt`. Licence: MIT (`LICENSE`).
