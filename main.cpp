// ============================================================
// Flappy Bird — SSD1306 128x64 OLED (ESP32, PlatformIO)
// Button: GPIO 5 (one leg to GPIO 5, other to GND)
// I2C:    SDA = GPIO 21, SCL = GPIO 22 (ESP32 defaults)
//
// platformio.ini lib_deps:
//   adafruit/Adafruit SSD1306 @ ^2.5.7
//   adafruit/Adafruit GFX Library @ ^1.11.5
// ============================================================

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Hardware ──────────────────────────────────────────────────
#define SCREEN_W    128
#define SCREEN_H    64
#define OLED_RESET  -1
#define BUTTON_PIN  5

Adafruit_SSD1306 display(SCREEN_W, SCREEN_H, &Wire, OLED_RESET);

// ── Physics / layout constants ────────────────────────────────
#define GRAVITY       0.2f
#define JUMP_VEL     -2.f
#define BIRD_X        22
#define BIRD_W        10
#define BIRD_H        8
#define PIPE_W        12
#define PIPE_GAP      30      // vertical gap between top & bottom pipe
#define PIPE_SPEED    2
#define NUM_PIPES     3
#define PIPE_SPACING  55      // horizontal distance between pipes
#define GROUND_Y      (SCREEN_H - 7)

// ── Bird bitmaps (10x8, horizontal, image2cpp) ────────────────
// Each row = 2 bytes, only leftmost 10 bits used
// Wings UP
static const uint8_t PROGMEM birdUp[16] = {
  0x1C, 0x00,
  0x7F, 0x00,
  0xFF, 0x80,
  0xF9, 0x80,
  0xFF, 0xC0,
  0xFF, 0x80,
  0x7F, 0x00,
  0x1C, 0x00
};

// Wings MID
static const uint8_t PROGMEM birdMid[16] = {
  0x1C, 0x00,
  0x7F, 0x00,
  0xFF, 0x80,
  0xF9, 0x80,
  0xFF, 0xC0,
  0xFF, 0x80,
  0x7E, 0x00,
  0x3C, 0x00
};

// Wings DOWN
static const uint8_t PROGMEM birdDown[16] = {
  0x3C, 0x00,
  0x7F, 0x00,
  0xFF, 0x80,
  0xF9, 0x80,
  0xFF, 0xC0,
  0xFF, 0x80,
  0x3E, 0x00,
  0x08, 0x00
};

// ── Pipe / particle structs ───────────────────────────────────
struct Pipe {
  int16_t x;
  int16_t gapY;   // Y of the top of the opening
  bool    scored;
};

struct Particle {
  float x, y, vx, vy;
  uint8_t life;   // countdown frames
};

// ── Game state ────────────────────────────────────────────────
enum State { S_START, S_PLAY, S_DEAD, S_GAMEOVER };
State state = S_START;

float    birdY      = 26.0f;
float    birdVel    = 0.0f;
uint8_t  wingFrame  = 0;        // 0=up, 1=mid, 2=down
uint8_t  wingTick   = 0;

Pipe     pipes[NUM_PIPES];
bool     launched   = false;   // false = bird frozen until first jump
uint16_t score      = 0;
uint16_t highScore  = 0;

uint32_t deadTimer  = 0;        // time bird died, for delay before gameover screen
uint32_t flashTimer = 0;
uint32_t lastFrame  = 0;
bool     flashOn    = false;

#define NUM_PARTICLES 8
Particle particles[NUM_PARTICLES];

// ── Button debounce ───────────────────────────────────────────
// with this:
bool rawState    = HIGH;
bool stableState = HIGH;
bool     btnFired      = false;
uint32_t lastDebounce  = 0;
#define  DEBOUNCE_MS   40

// ── Helpers ───────────────────────────────────────────────────
void spawnPipe(uint8_t i, int16_t startX) {
  pipes[i].x      = startX;
  pipes[i].gapY   = random(6, GROUND_Y - PIPE_GAP - 6);
  pipes[i].scored = false;
}

void initPipes() {
  for (uint8_t i = 0; i < NUM_PIPES; i++) {
    spawnPipe(i, SCREEN_W + 20 + i * PIPE_SPACING);
  }
}

void spawnParticles(float x, float y) {
  for (uint8_t i = 0; i < NUM_PARTICLES; i++) {
    float angle       = (TWO_PI / NUM_PARTICLES) * i;
    float spd         = 1.2f + (random(0, 80) / 100.0f);
    particles[i].x    = x;
    particles[i].y    = y;
    particles[i].vx   = cos(angle) * spd;
    particles[i].vy   = sin(angle) * spd;
    particles[i].life = 14;
  }
}

void pollButton() {
  bool reading = digitalRead(BUTTON_PIN);

  if (reading != rawState) {
    lastDebounce = millis();
    rawState = reading;
  }

  if ((millis() - lastDebounce) > DEBOUNCE_MS) {
    if (reading == LOW && stableState == HIGH) {
      btnFired = true;
    }
    stableState = reading;
  }
}

bool collision() {
  if (birdY + BIRD_H >= GROUND_Y) return true;
  if (birdY <= 0)                  return true;
  for (uint8_t i = 0; i < NUM_PIPES; i++) {
    // horizontal overlap (shrink hitbox 2px each side)
    if (BIRD_X + BIRD_W - 2 > pipes[i].x + 2 &&
        BIRD_X + 2          < pipes[i].x + PIPE_W - 2) {
      // vertical: outside the gap?
      if (birdY < pipes[i].gapY ||
          birdY + BIRD_H > pipes[i].gapY + PIPE_GAP) {
        return true;
      }
    }
  }
  return false;
}

// ── Draw routines ─────────────────────────────────────────────
void drawGround() {
  display.fillRect(0, GROUND_Y, SCREEN_W, SCREEN_H - GROUND_Y, SSD1306_WHITE);
  // small notch pattern on top of ground
  for (uint8_t x = 0; x < SCREEN_W; x += 6) {
    display.drawPixel(x + 2, GROUND_Y, SSD1306_BLACK);
  }
}

void drawPipe(const Pipe& p) {
  int16_t topH  = p.gapY;
  int16_t botY  = p.gapY + PIPE_GAP;
  int16_t botH  = GROUND_Y - botY;

  // filled body
  display.fillRect(p.x, 0,    PIPE_W, topH, SSD1306_WHITE);
  display.fillRect(p.x, botY, PIPE_W, botH, SSD1306_WHITE);

  // end caps (2px wider each side)
  display.fillRect(p.x - 2, topH - 5, PIPE_W + 4, 5, SSD1306_WHITE);
  display.fillRect(p.x - 2, botY,     PIPE_W + 4, 5, SSD1306_WHITE);

  // inner highlight (makes it look 3-D)
  display.drawFastVLine(p.x + 2, 0,    topH,       SSD1306_BLACK);
  display.drawFastVLine(p.x + 2, botY, botH,       SSD1306_BLACK);
}

void drawBird(int16_t y, uint8_t frame) {
  const uint8_t* bmp;
  if      (frame == 0) bmp = birdUp;
  else if (frame == 1) bmp = birdMid;
  else                 bmp = birdDown;
  display.drawBitmap(BIRD_X, y, bmp, BIRD_W, BIRD_H, SSD1306_WHITE);
}

void drawParticles() {
  for (uint8_t i = 0; i < NUM_PARTICLES; i++) {
    if (particles[i].life == 0) continue;
    display.drawPixel((int16_t)particles[i].x, (int16_t)particles[i].y, SSD1306_WHITE);
    // draw a 2x2 dot for bigger particles early in life
    if (particles[i].life > 7) {
      display.drawPixel((int16_t)particles[i].x + 1, (int16_t)particles[i].y, SSD1306_WHITE);
      display.drawPixel((int16_t)particles[i].x, (int16_t)particles[i].y + 1, SSD1306_WHITE);
    }
  }
}

void updateParticles() {
  for (uint8_t i = 0; i < NUM_PARTICLES; i++) {
    if (particles[i].life == 0) continue;
    particles[i].x  += particles[i].vx;
    particles[i].y  += particles[i].vy;
    particles[i].vy += 0.2f;  // particle gravity
    particles[i].life--;
  }
}

void drawScore(uint16_t s, uint8_t x, uint8_t y, uint8_t sz = 1) {
  display.setTextSize(sz);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(x, y);
  display.print(s);
}

// ── State: START ──────────────────────────────────────────────
void drawStartScreen() {
  display.clearDisplay();

  // title
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(10, 2);
  display.print("FLAPPY");
  display.setCursor(22, 20);
  display.print("BIRD!");

  // idle bird
  drawBird(36, wingFrame);

  // blinking prompt
  if (flashOn) {
    display.setTextSize(1);
    display.setCursor(46, 38);
    display.print("PRESS");
    display.setCursor(40, 48);
    display.print("TO START");
  }

  // high score
  if (highScore > 0) {
    display.setTextSize(1);
    display.setCursor(70, 22);
    display.print("HI: ");
    display.print(highScore);
  }

  display.display();
}

// ── State: PLAY ───────────────────────────────────────────────
void drawPlayScreen() {
  display.clearDisplay();
  drawGround();

  for (uint8_t i = 0; i < NUM_PIPES; i++) drawPipe(pipes[i]);
  drawBird((int16_t)birdY, wingFrame);

  // score top-center
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  // center roughly
  uint8_t digits = score < 10 ? 1 : score < 100 ? 2 : 3;
  display.setCursor(SCREEN_W / 2 - (digits * 3), 1);
  display.print(score);

  display.display();
}

// ── State: DEAD (brief death animation before gameover) ───────
void drawDeadScreen() {
  display.clearDisplay();
  drawGround();
  for (uint8_t i = 0; i < NUM_PIPES; i++) drawPipe(pipes[i]);
  // bird falls, wings down
  drawBird((int16_t)birdY, 2);
  drawParticles();

  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  uint8_t digits = score < 10 ? 1 : score < 100 ? 2 : 3;
  display.setCursor(SCREEN_W / 2 - (digits * 3), 1);
  display.print(score);

  display.display();
}

// ── State: GAMEOVER ───────────────────────────────────────────
void drawGameOverScreen() {
  display.clearDisplay();

  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(2, 2);
  display.print("GAME OVER");

  display.drawFastHLine(0, 20, SCREEN_W, SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(16, 26);
  display.print("SCORE : ");
  display.print(score);

  display.setCursor(16, 38);
  display.print("BEST  : ");
  display.print(highScore);

  if (score >= highScore && score > 0) {
    display.setCursor(16, 50);
    display.print("** NEW BEST! **");
  } else if (flashOn) {
    display.setCursor(20, 54);
    display.print("PRESS TO RETRY");
  }

  display.display();
}

// ── State transitions ─────────────────────────────────────────
void startGame() {
  birdY      = 26.0f;
  birdVel    = 0.0f;
  score      = 0;
  wingFrame  = 0;
  wingTick   = 0;
  for (auto& p : particles) p.life = 0;
  launched   = false;
  initPipes();
  state = S_PLAY;
}

void triggerDeath() {
  state     = S_DEAD;
  deadTimer = millis();
  birdVel   = JUMP_VEL * 0.4f;   // small pop upward on death
  spawnParticles(BIRD_X + BIRD_W / 2.0f, birdY + BIRD_H / 2.0f);
  if (score > highScore) highScore = score;
}

// ── Setup ─────────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("SSD1306 init failed");
    for (;;);
  }

  display.clearDisplay();
  display.display();
  randomSeed(analogRead(34));   // floating pin for entropy
  rawState    = digitalRead(BUTTON_PIN);
  stableState = rawState;
}

// ── Loop ──────────────────────────────────────────────────────
void loop() {
  // ~60fps cap
  if (millis() - lastFrame < 16) return;
  lastFrame = millis();

  pollButton();

  // flash toggle every 500ms
  if (millis() - flashTimer > 500) {
    flashOn    = !flashOn;
    flashTimer = millis();
  }

  // wing animation (cycles every 6 frames: up→mid→down→mid)
  wingTick++;
  if (wingTick >= 6) {
    wingTick = 0;
    wingFrame = (wingFrame + 1) % 3;
  }

  // ── State machine ──────────────────────────────────────────
  switch (state) {

    case S_START:
      drawStartScreen();
      if (btnFired) startGame();
      break;

    case S_PLAY:
      // jump
      if (btnFired) {
        launched = true;
        birdVel  = JUMP_VEL;
      }

      // physics only after first jump
      if (launched) {
        birdVel += GRAVITY;
        birdY   += birdVel;
      }

      // scroll pipes + score
      for (uint8_t i = 0; i < NUM_PIPES; i++) {
        if (launched) pipes[i].x -= PIPE_SPEED;
        // score when bird passes pipe center
        if (!pipes[i].scored &&
            BIRD_X > pipes[i].x + PIPE_W / 2) {
          pipes[i].scored = true;
          score++;
        }
        // recycle pipe off left edge
        if (pipes[i].x + PIPE_W + 2 < 0) {
          // find the rightmost pipe to spawn after it
          int16_t maxX = 0;
          for (uint8_t j = 0; j < NUM_PIPES; j++) maxX = max(maxX, pipes[j].x);
          spawnPipe(i, maxX + PIPE_SPACING);
        }
      }

      if (collision()) triggerDeath();
      else             drawPlayScreen();
      break;

    case S_DEAD:
      // bird still falls with gravity
      birdVel += GRAVITY;
      birdY   += birdVel;
      if (birdY + BIRD_H > GROUND_Y) {
        birdY   = GROUND_Y - BIRD_H;
        birdVel = 0;
      }
      updateParticles();
      drawDeadScreen();
      // after 1.8s, go to gameover screen
      if (millis() - deadTimer > 1800) state = S_GAMEOVER;
      break;

    case S_GAMEOVER:
      drawGameOverScreen();
      if (btnFired) startGame();
      break;
  }

  btnFired = false;   // consume the event
}
