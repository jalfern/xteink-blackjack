// Host test for the Blackjack rules engine. Runs on macOS, no hardware:
//
//   clang++ -std=c++17 -I src tools/logic_test.cpp -o /tmp/bjtest && /tmp/bjtest
//
// This is the only part of the project that can be verified before the device is
// involved, so it covers the things that would otherwise be silent on screen.
#include "blackjack.h"
#include <stdio.h>
#include <stdlib.h>

static int fails = 0, checks = 0;

#define CHECK(cond, ...) do {                                   \
    checks++;                                                   \
    if (!(cond)) { fails++; printf("  FAIL  ");                \
                   printf(__VA_ARGS__); printf("\n      at %s:%d  %s\n", \
                   __FILE__, __LINE__, #cond); }                \
  } while (0)

using namespace bj;

static int card(const char* rank, int suit) {
  for (int r = 0; r < RANKS; r++)
    if (strcmp(rankName(r), rank) == 0) return r * SUITS + suit;
  printf("  !! bad rank %s\n", rank); exit(2);
}

int main() {
  printf("== card values ==\n");
  {
    Hand h; h.add(card("A", 0));
    bool soft = false;
    CHECK(h.total(&soft) == 21 || h.total(&soft) == 11, "ace alone");
    CHECK(h.total(&soft) == 11 && soft, "ace alone is 11 soft");
  }
  { Hand h; h.add(card("A",0)); h.add(card("A",1)); bool s=false;
    // A A is SOFT 12 - one ace is still valued 11, so hitting cannot bust.
    // (An earlier version of this test asserted "hard" and was itself wrong.)
    CHECK(h.total(&s)==12 && s, "A A = 12 soft, got %d soft=%d", h.total(&s), s); }
  { Hand h; h.add(card("A",0)); h.add(card("6",1)); bool s=false;
    CHECK(h.total(&s)==17 && s, "A 6 = 17 soft"); }
  { Hand h; h.add(card("A",0)); h.add(card("6",1)); h.add(card("10",2)); bool s=true;
    CHECK(h.total(&s)==17 && !s, "A 6 10 = 17 hard, got %d soft=%d", h.total(&s), s); }
  { Hand h; h.add(card("K",0)); h.add(card("J",1)); h.add(card("9",2));
    CHECK(h.total()==29 && h.busted(), "K J 9 = 29 bust"); }
  { Hand h; h.add(card("A",0)); h.add(card("5",1)); h.add(card("6",2));
    CHECK(h.total()==12 && !h.busted(), "A 5 6 = 12"); }
  { Hand h; h.add(card("A",0)); h.add(card("5",1)); h.add(card("6",2)); h.add(card("K",3));
    // 11+5+6+10 = 32, ace drops to 1 -> 22, still bust
    CHECK(h.total()==22 && h.busted(), "A 5 6 K = 22 bust, got %d", h.total()); }

  printf("== every rank maps to the right value ==\n");
  {
    const int want[RANKS] = {11,2,3,4,5,6,7,8,9,10,10,10,10};
    for (int r = 0; r < RANKS; r++) {
      Hand h; h.add(r * SUITS + 0); Hand t; t.add(r*SUITS); t.add(card("2",1));
      // ace with a 2 is 13 soft, everything else is value+2
      int expect = (r == 0) ? 13 : want[r] + 2;
      CHECK(t.total() == expect, "rank %s + 2 => %d, got %d", rankName(r), expect, t.total());
    }
  }

  printf("== blackjack vs 3-card 21 ==\n");
  { Hand h; h.add(card("A",0)); h.add(card("K",1));
    CHECK(h.isBlackjack(), "A K is a blackjack"); }
  { Hand h; h.add(card("A",0)); h.add(card("5",1)); h.add(card("5",2));
    CHECK(h.total()==21 && !h.isBlackjack(), "A 5 5 is 21 but not a blackjack"); }

  printf("== dealer stands on 17 (incl. soft 17) ==\n");
  {
    Game g(12345);
    g.shuffle();
    // force a dealer hand by hand
    g.dealer.add(card("A",0)); g.dealer.add(card("6",1));   // soft 17
    bool soft = false; g.dealer.total(&soft);
    CHECK(soft && g.dealer.total() == 17, "set up soft 17");
    g.playDealer();
    CHECK(g.dealer.n == 2, "dealer must NOT hit soft 17 (drew %d cards)", g.dealer.n - 2);
  }

  printf("== settlement arithmetic ==\n");
  {
    Game g(7); g.shuffle(); g.bankroll = 100; g.bet = 10;
    // player 20, dealer 19 -> win 10
    g.player.add(card("K",0)); g.player.add(card("K",1));
    g.dealer.add(card("K",2)); g.dealer.add(card("K",3)); g.dealer.add(card("9",0));
    g.phase = Phase::PLAYER; g.stand();
    CHECK(g.bankroll == 110, "win pays 1:1, bankroll %d", g.bankroll);

    // blackjack pays 3:2
    g.bankroll = 100; g.bet = 10; g.phase = Phase::BET;
    g.player.clear(); g.dealer.clear();
    g.player.add(card("A",0)); g.player.add(card("K",1));
    g.dealer.add(card("9",2)); g.dealer.add(card("9",3));
    g.settle();
    CHECK(g.bankroll == 115, "blackjack pays 3:2, got +%d", g.bankroll - 100);

    // push
    g.bankroll = 100; g.bet = 10; g.phase = Phase::PLAYER;
    g.player.clear(); g.dealer.clear();
    g.player.add(card("K",0)); g.player.add(card("9",1));
    g.dealer.add(card("K",2)); g.dealer.add(card("9",3));
    g.stand();
    CHECK(g.bankroll == 100, "push returns the bet, bankroll %d", g.bankroll);

    // bust
    g.bankroll = 100; g.bet = 10; g.phase = Phase::PLAYER;
    g.player.clear(); g.dealer.clear();
    g.player.add(card("K",0)); g.player.add(card("9",1)); g.player.add(card("9",2));
    g.hit();
    CHECK(g.player.busted(), "hit put player over 21");
    CHECK(g.bankroll == 90, "bust loses the bet, bankroll %d", g.bankroll);
  }

  printf("== bet clamping ==\n");
  {
    Game g(1); g.bankroll = 1000; g.bet = 1; g.changeBet(-100);
    CHECK(g.bet == Game::MIN_BET, "bet floors at %d, got %d", Game::MIN_BET, g.bet);
    g.changeBet(+1000);
    CHECK(g.bet == Game::MAX_BET, "bet caps at %d, got %d", Game::MAX_BET, g.bet);
    g.bankroll = 3; g.changeBet(+100);
    CHECK(g.bet <= 3, "bet cannot exceed bankroll (%d > %d)", g.bet, g.bankroll);
    CHECK(!g.canBet(50), "canBet rejects 50");
  }

  printf("== every one of the 312 shoe cards decodes correctly ==\n");
  {
    // Regression: rank was once computed as card/SUITS without %RANKS, so any
    // card from deck 1 onward decoded to a rank above 12. Unit tests that only
    // built hands from deck-0 card ids passed happily while the real shoe was
    // broken - so this loop walks all 312.
    for (int c = 0; c < DEPTH; c++) {
      int r = rankOf(c);
      CHECK(r >= 0 && r < RANKS, "card %d -> rank %d out of range", c, r);
      CHECK(suitOf(c) >= 0 && suitOf(c) < SUITS, "card %d bad suit", c);
      Hand h; h.add(c);
      int want = (r == 0) ? 11 : (r >= 9 ? 10 : r + 1);
      CHECK(h.total() == want, "card %d rank %s total %d want %d", c, rankName(r), h.total(), want);
    }
    // and known face cards late in the shoe: deck 5, K and Q of suit 2
    int k = 5 * RANKS * SUITS + 12 * SUITS + 2;
    int q = 5 * RANKS * SUITS + 11 * SUITS + 2;
    CHECK(rankOf(k) == 12 && rankOf(q) == 11, "deck-5 face ranks decode (%d,%d)", rankOf(k), rankOf(q));
    Hand h; h.add(k); h.add(q);
    CHECK(h.total() == 20, "K+Q from deck 5 = 20, got %d", h.total());
  }

  printf("== shoe integrity + 200k-hand soak ==\n");
  {
    Game g(20260915);
    g.shuffle();
    // every card exactly once per shuffle
    bool seen[DEPTH]; memset(seen, 0, sizeof(seen));
    for (int i = 0; i < DEPTH; i++) { int c = g.shoe[i]; CHECK(!seen[c], "duplicate card in shoe"); seen[c] = true; }

    int hands = 0, busts = 0, bjs = 0, pushes = 0, dbusts = 0, wins = 0, losses = 0;
    int minBank = 1 << 30, maxBank = 0;
    for (int i = 0; i < 200000; i++) {
      if (g.bankroll < Game::MIN_BET) g.bankroll = 100;
      g.bet = 5;
      if (!g.deal()) { printf("  FAIL  deal() refused with bankroll %d\n", g.bankroll); fails++; break; }
      // naive basic strategy
      while (g.phase == Phase::PLAYER && g.player.total() < 17) g.hit();
      if (g.phase == Phase::PLAYER) g.stand();
      CHECK(g.phase == Phase::RESULT, "hand did not reach RESULT");
      CHECK(g.bankroll >= 0, "bankroll went negative: %d", g.bankroll);
      CHECK(g.player.total() <= 40, "impossible total %d", g.player.total());
      CHECK(g.dealer.total() <= 40, "impossible dealer total %d", g.dealer.total());
      switch (g.outcome) {
        case Outcome::BLACKJACK:    bjs++;    wins++;  break;
        case Outcome::PLAYER_WINS:  wins++;            break;
        case Outcome::DEALER_BUST:  dbusts++; wins++;  break;
        case Outcome::PLAYER_BUST:  busts++;  losses++; break;
        case Outcome::DEALER_WINS:  losses++;          break;
        case Outcome::PUSH:         pushes++;          break;
        default: CHECK(false, "outcome left NONE"); break;
      }
      hands++;
      if (g.bankroll < minBank) minBank = g.bankroll;
      if (g.bankroll > maxBank) maxBank = g.bankroll;
    }
    printf("  %d hands: win %d (bj %d, dealer-bust %d)  lose %d (bust %d)  push %d\n",
           hands, wins, bjs, dbusts, losses, busts, pushes);
    printf("  bankroll range %d..%d\n", minBank, maxBank);
    CHECK(hands == 200000, "soak completed (%d)", hands);

    // Sanity bands for a naive hit-until-17 bot. If shuffling, ace handling or
    // settlement were wrong these swing far outside the band, so this is a real
    // signal and not decoration.
    double bjPct   = 100.0 * bjs    / hands;
    double pushPct = 100.0 * pushes / hands;
    double bustPct = 100.0 * busts  / hands;
    printf("  rates: bj %.2f%%  push %.2f%%  player-bust %.2f%%\n", bjPct, pushPct, bustPct);
    CHECK(bjPct   > 2.0 && bjPct   < 8.0,  "blackjack rate %.2f%% implausible", bjPct);
    CHECK(pushPct > 3.0 && pushPct < 20.0, "push rate %.2f%% implausible", pushPct);
    CHECK(bustPct > 15.0 && bustPct < 45.0,"bust rate %.2f%% implausible", bustPct);
    // A naive hit-everything-to-17 bot plays with a large house edge, so it
    // WILL eventually bust out - that is the edge working, not a bug. The real
    // signals are that the bankroll moves in both directions and never goes
    // negative or gains impossible amounts.
    CHECK(minBank >= 0, "bankroll went negative (min %d)", minBank);
    CHECK(maxBank > minBank, "bankroll never moved (min %d max %d)", minBank, maxBank);
    CHECK(maxBank < 100000, "bankroll implausibly large: %d", maxBank);
  }

  printf("\n%d checks, %d failures\n", checks, fails);
  return fails ? 1 : 0;
}
