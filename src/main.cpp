// Xteink X3 Blackjack - application entry.  Portrait 528x792.
//
// The panel RAM is physically 792x528; portrait is a transpose applied when
// pixels are drawn (see panel.h EPD_PORTRAIT). All coordinates in this file are
// logical portrait.
#include <Arduino.h>
#include <string.h>
#include <esp_ota_ops.h>
#include <esp_system.h>

#include "panel.h"
#include "input.h"
#include "gfx.h"
#include "blackjack.h"

using namespace epd;
using keys::Key;

static bj::Game g_game(0);

enum Screen { SCR_MENU, SCR_KEYS, SCR_PANEL, SCR_BET, SCR_GAME };
static Screen s_screen = SCR_MENU;
static int s_menu = 0;
static const char* MENU[] = {"BLACKJACK", "KEY TEST", "PANEL TEST"};
static constexpr int MENU_N = 3;

// Cards: 5 in hand = 120 + 4*42 = 288 wide, centred in 528.
static constexpr int CARD_W = 120, CARD_H = 170, CARD_STEP = 42;
static constexpr int DEALER_Y = 108, PLAYER_Y = 366;

// ------------------------------------------------------------------ input

static void quitToReader() {
  const esp_partition_t* app0 = esp_ota_get_next_update_partition(NULL);
  if (app0 && strcmp(app0->label, "app0") == 0) {
    Serial.println("BOOT: returning to app0 (CrossPoint)");
    epd::deepSleep();
    esp_ota_set_boot_partition(app0);
    esp_restart();
  } else {
    Serial.println("BOOT: next partition is not app0 - NOT rebooting");
  }
}

// Single place that waits for input, so the POWER hold is always being checked.
// (The original bug: POWER was tested in loop(), but every screen blocks here.)
static Key wait() {
  for (;;) {
    Key k = keys::pollKey();
    if (keys::powerHeldFor(1500)) quitToReader();
    if (k != Key::NONE) return k;
    delay(5);
  }
}

// ------------------------------------------------------------------ paint

static void showFull() { refreshFull(); }

static void showFast() {
  if (g_dirty >= DIRTY_LIMIT) refreshHalf();
  else refreshFast();
}

static void banner(const char* title, const char* right) {
  gfx::fill(0, 0, W, 60, false);
  gfx::text(16, 16, title, F_MED, true);
  if (right) gfx::textR(W - 16, 16, right, F_MED, true);
}

// A labelled action pill. The panel is static, so the controls are drawn as
// explicit boxes: an e-ink screen showing nothing at all is indistinguishable
// from a hang, and that already cost this project a false alarm.
static void pill(int x, int y, int w, int h, const char* key, const char* act, bool hot) {
  if (hot) { gfx::fill(x, y, w, h, false); gfx::frame(x, y, w, h, true); }
  else     { gfx::fill(x, y, w, h, true);  gfx::frame(x, y, w, h, false); }
  gfx::frame(x + 3, y + 3, w - 6, h - 6, hot);
  gfx::textCX(x + w / 2, y + h / 2 - 14, act, F_MED, hot);
  gfx::textCX(x + w / 2, y + h / 2 + 22, key, F_SMALL, hot);
}

// ------------------------------------------------------------------ menu

static void drawMenu() {
  clear(true);
  gfx::textC(48, "BLACK", F_HUGE);
  gfx::textC(124, "JACK", F_HUGE);
  gfx::hline(64, 214, W - 128, false);

  int y = 250;
  for (int i = 0; i < MENU_N; i++) {
    gfx::menuItem(54, y, W - 108, MENU[i], i == s_menu, F_BIG);
    y += 96;
  }
  gfx::textC(600, "UP / DOWN   move", F_MED);
  gfx::textC(640, "CONFIRM     select", F_MED);
  gfx::hline(64, 676, W - 128, false);
  gfx::textC(700, "HOLD POWER 1.5s", F_MED);
  gfx::textC(736, "RETURN TO READER", F_SMALL);
  showFull();
}

// ------------------------------------------------------------------ tests

static void keysLoop() {
  clear(true);
  banner("KEY TEST", "CONFIRM");
  gfx::textC(80, "PRESS EACH KEY", F_MED);

  gfx::text(48, 148, "KEY", F_MED);
  gfx::text(300, 148, "PRESSED", F_MED);
  gfx::hline(40, 184, W - 80, false);
  showFull();

  char line[96];
  const Key order[] = { Key::BACK, Key::CONFIRM, Key::LEFT,
                        Key::RIGHT, Key::UP, Key::DOWN, Key::POWER };
  int y = 200;
  for (int i = 0; i < 7; i++) {
    snprintf(line, sizeof(line), "%-8s", keys::name(order[i]));
    gfx::text(48, y, line, F_MED);
    y += 44;
  }

  int v1 = 0, v2 = 0;
  for (;;) {
    v1 = analogRead(keys::PIN_ADC1);
    v2 = analogRead(keys::PIN_ADC2);
    Key k = keys::pollKey();
    if (k == Key::CONFIRM || k == Key::BACK) return;
    if (k != Key::NONE) Serial.printf("KEY %s adc1=%d adc2=%d\n", keys::name(k), v1, v2);

    gfx::fill(280, 200, 220, 7 * 44, true);
    if (k != Key::NONE) {
      for (int i = 0; i < 7; i++)
        if (order[i] == k) gfx::text(300, 200 + i * 44, "YES", F_MED);
    }
    snprintf(line, sizeof(line), "ADC1 %4d   ADC2 %4d", v1, v2);
    gfx::fill(0, 560, W, 60, true);
    gfx::hline(0, 560, W, false);
    gfx::text(48, 572, line, F_MED);
    showFast();
    delay(5);
  }
}

static void timed(const char* label, void (*fn)()) {
  uint32_t t0 = millis();
  fn();
  uint32_t ms = millis() - t0;
  char b[64];
  snprintf(b, sizeof(b), "%-6s %5lu ms", label, (unsigned long)ms);
  Serial.println(b);
  gfx::fill(60, 470, W - 120, 50, true);
  gfx::text(70, 480, b, F_MED);
  showFast();
}

static void panelLoop() {
  clear(true);
  gfx::textC(300, "PANEL", F_HUGE);
  showFull();

  for (int band = 0; band < 4; band++) {
    clear(true);
    // Bands run across the logical portrait screen, so a stuck source driver
    // shows up as a dead vertical strip rather than being hidden by rotation.
    for (int i = 0; i < 4; i++) gfx::fill(i * (W / 4), 0, W / 4, H, i != band);
    showFast();
    delay(350);
  }

  clear(true);
  for (int y = 0; y < H; y += 8)
    for (int x = ((y / 8) & 1) ? 8 : 0; x < W; x += 16) gfx::px(x, y, false);
  gfx::fill(130, 340, 268, 110, true);
  gfx::textCX(W / 2, 395, "8 PX", F_BIG);
  showFull();
  delay(350);

  clear(true);
  gfx::textC(150, "TIMING", F_HUGE);
  gfx::text(70, 480, "measuring...", F_MED);
  showFull();
  timed("fast", refreshFast);
  timed("half", refreshHalf);
  timed("full", refreshFull);
  gfx::textC(620, "CONFIRM FOR MENU", F_MED);
  showFast();

  for (;;) {
    Key k = wait();
    if (k == Key::CONFIRM || k == Key::BACK) return;
  }
}

// ------------------------------------------------------------------ betting

static void drawBet() {
  clear(true);
  char b[48];
  banner("PLACE BET", NULL);

  snprintf(b, sizeof(b), "BANK %d", g_game.bankroll);
  gfx::textR(W - 16, 76, b, F_MED);

  gfx::frame(90, 170, W - 180, 260, false);
  gfx::frame(98, 178, W - 196, 276, false);
  gfx::textCX(W / 2, 230, "BET", F_MED);
  snprintf(b, sizeof(b), "%d", g_game.bet);
  gfx::textCX(W / 2, 320, b, F_HUGE);

  pill(60, 500, 190, 110, "LEFT",  "LESS",  false);
  pill(278, 500, 190, 110, "RIGHT", "MORE",  false);
  gfx::textC(650, "CONFIRM  DEAL", F_MED);
  gfx::textC(692, "BACK     MENU", F_MED);

  if (g_game.bankroll < bj::Game::MIN_BET) {
    gfx::fill(0, 740, W, 44, false);
    gfx::textC(748, "OUT OF CHIPS - CONFIRM RESETS", F_MED, true);
  }
  showFull();
}

static void betLoop() {
  drawBet();
  for (;;) {
    Key k = wait();
    if (k == Key::BACK) { s_screen = SCR_MENU; return; }
    if (k == Key::LEFT) g_game.changeBet(-5);
    if (k == Key::RIGHT) g_game.changeBet(+5);
    if (k == Key::CONFIRM) {
      if (g_game.bankroll < bj::Game::MIN_BET) {
        g_game.bankroll = 100;
        g_game.shuffle();
        drawBet();
        continue;
      }
      g_game = bj::Game((uint64_t)millis());
      g_game.shuffle();
      g_game.bankroll = 100;
      g_game.bet = 5;
      g_game.deal();
      s_screen = SCR_GAME;
      return;
    }
    if (k == Key::LEFT || k == Key::RIGHT) drawBet();
  }
}

// ------------------------------------------------------------------ game

static void drawHand(const bj::Hand& h, int y, bool hideHole) {
  int x = (W - (CARD_W + (h.n - 1) * CARD_STEP)) / 2;
  for (int i = 0; i < h.n; i++) {
    bool back = hideHole && i == 1;
    gfx::card(x + i * CARD_STEP, y, CARD_W, CARD_H,
              back ? "" : bj::rankName(bj::rankOf(h.cards[i])),
              bj::suitOf(h.cards[i]), back);
  }
}

static void drawGame() {
  char b[64];
  clear(true);

  snprintf(b, sizeof(b), "BET %d  BANK %d", g_game.bet, g_game.bankroll);
  banner("BLACKJACK", b);

  bool dSoft = false;
  int dt = g_game.dealer.total(&dSoft);
  bool shown = (g_game.phase != bj::Phase::PLAYER);

  gfx::text(20, 74, "DEALER", F_SMALL);
  if (shown) snprintf(b, sizeof(b), "%s%d", dSoft ? "SOFT " : "", dt);
  else snprintf(b, sizeof(b), "SHOWS %s", bj::rankName(bj::rankOf(g_game.dealer.cards[0])));
  gfx::textR(W - 20, 74, b, F_SMALL);
  drawHand(g_game.dealer, DEALER_Y, !shown);

  gfx::hline(60, 300, W - 120, false);

  bool pSoft = false;
  int pt = g_game.player.total(&pSoft);
  gfx::text(20, 332, "YOU", F_SMALL);
  snprintf(b, sizeof(b), "%s%d", pSoft ? "SOFT " : "", pt);
  gfx::textR(W - 20, 332, b, F_SMALL);
  drawHand(g_game.player, PLAYER_Y, false);

  bool playing = (g_game.phase == bj::Phase::PLAYER);

  if (playing) {
    bool canD = g_game.canDouble();
    pill(14, 596, 160, 130, "UP", "HIT", true);
    pill(184, 596, 160, 130, "DOWN", "STAND", true);
    pill(354, 596, 160, 130, "CONFIRM", canD ? "DOUBLE" : "-", canD);
    gfx::textC(744, "BACK TO BET", F_SMALL);
  } else {
    gfx::fill(30, 570, W - 60, 150, false);
    gfx::frame(36, 576, W - 72, 138, true);
    gfx::textCX(W / 2, 630, g_game.outcomeText(), F_BIG, true);
    int profit = 0;
    if (g_game.outcome == bj::Outcome::BLACKJACK) profit = (g_game.bet * 3) / 2;
    else if (g_game.outcome == bj::Outcome::PLAYER_WINS ||
             g_game.outcome == bj::Outcome::DEALER_BUST) profit = g_game.bet;
    else if (g_game.outcome == bj::Outcome::PLAYER_BUST ||
             g_game.outcome == bj::Outcome::DEALER_WINS) profit = -g_game.bet;
    char p[32];
    snprintf(p, sizeof(p), "%s%d", profit >= 0 ? "+" : "", profit);
    gfx::textCX(W / 2, 682, p, F_MED, true);
    gfx::textC(754, "CONFIRM  NEW HAND", F_SMALL);
  }
}

static void gameLoop() {
  drawGame();
  showFull();

  while (g_game.phase == bj::Phase::PLAYER) {
    Key k = wait();
    if (k == Key::BACK) { s_screen = SCR_BET; return; }
    if (k == Key::UP) g_game.hit();
    else if (k == Key::DOWN) g_game.stand();
    else if (k == Key::CONFIRM) { if (g_game.canDouble()) g_game.dbl(); else g_game.hit(); }
    else continue;
    drawGame();
    showFast();
  }

  drawGame();
  showFast();
  for (;;) {
    Key k = wait();
    if (k == Key::CONFIRM || k == Key::BACK) { s_screen = SCR_BET; return; }
  }
}

// ------------------------------------------------------------------ entry

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.printf("\nX3 Blackjack - boot (EPD_ROT=%d, logical %dx%d)\n",
                EPD_ROT, W, H);

  const esp_partition_t* me = esp_ota_get_running_partition();
  const esp_partition_t* nxt = esp_ota_get_next_update_partition(NULL);
  if (me) {
    Serial.printf("BOOT: running from '%s' @0x%X size=%u\n", me->label,
                  (unsigned)me->address, (unsigned)me->size);
    if (strcmp(me->label, "app0") == 0)
      Serial.println("BOOT: WARNING app0 - this should be CrossPoint!");
  }
  if (nxt) Serial.printf("BOOT: 'back to reader' selects '%s' @0x%X\n",
                         nxt->label, (unsigned)nxt->address);

  keys::begin();
  epd::begin();
  g_game = bj::Game(0);
  g_game.shuffle();
  drawMenu();
}

void loop() {
  if (keys::powerHeldFor(1500)) quitToReader();

  Key k = keys::pollKey();
  switch (s_screen) {
    case SCR_MENU:
      if (k == Key::UP || k == Key::LEFT) { s_menu = (s_menu + MENU_N - 1) % MENU_N; drawMenu(); }
      if (k == Key::DOWN || k == Key::RIGHT) { s_menu = (s_menu + 1) % MENU_N; drawMenu(); }
      if (k == Key::CONFIRM)
        s_screen = (s_menu == 0) ? SCR_BET : (s_menu == 1) ? SCR_KEYS : SCR_PANEL;
      break;
    case SCR_KEYS:  keysLoop();  s_screen = SCR_MENU; drawMenu(); break;
    case SCR_PANEL: panelLoop(); s_screen = SCR_MENU; drawMenu(); break;
    case SCR_BET:   betLoop();
                    if (s_screen == SCR_MENU) drawMenu();
                    else if (s_screen == SCR_GAME) gameLoop();
                    break;
    case SCR_GAME:  gameLoop();
                    if (s_screen == SCR_BET) betLoop();
                    if (s_screen == SCR_MENU) drawMenu();
                    break;
  }
  delay(8);
}
