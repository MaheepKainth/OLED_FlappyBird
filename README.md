Flappy Bird — ESP32 SSD1306 OLED
A fully playable Flappy Bird clone running on an ESP32 with a 128x64 SSD1306 OLED display and a single pushbutton.

Hardware
ComponentDetailsMicrocontrollerDOIT ESP32 DEVKIT V1 (30-pin)DisplaySSD1306 128x64 OLED (I2C)InputMomentary pushbutton
Wiring
ESP32 PinConnects ToGPIO 21 (SDA)OLED SDAGPIO 22 (SCL)OLED SCL3.3VOLED VCCGNDOLED GNDGPIO 5Button leg 1GNDButton leg 2

No resistor needed on the button — INPUT_PULLUP is used in firmware.


Software

Environment: PlatformIO + VS Code
platformio.ini:
ini[env:esp32doit-devkit-v1]
platform = espressif32
board = esp32doit-devkit-v1
framework = arduino
lib_deps =
  adafruit/Adafruit SSD1306 @ ^2.5.7
  adafruit/Adafruit GFX Library @ ^1.11.5

How to Play

Power on → start screen appears
Press button to start
Press button to flap — bird hovers frozen until first press
Navigate through pipe gaps
Score increments each time you pass a pipe
Hit a pipe or the ground → death animation plays → game over screen shows score and best
Press button to retry

Features

Start screen with idle flapping bird animation and high score
Bird frozen on spawn until first jump
Live score display during gameplay
Death particle burst on collision
Bird falls to ground after death before game over screen
Game over screen with current score, best score, and "NEW BEST!" flag
High score persists across rounds (resets on power cycle)
Non-blocking — everything runs on millis(), no delay() calls


Bird Bitmaps
Sprites are 10x8px in horizontal format (image2cpp). To use custom sprites:

Go to image2cpp
Upload a 10x8 PNG
Set output format to Arduino code, code format to Horizontal
Replace the birdUp, birdMid, birdDown arrays in the source


