#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Blackjack rules engine. Deliberately pure C++ with no Arduino/ESP includes:
// it must be compilable and testable on a host (see tools/logic_test.cpp),
// because a mis-scored soft ace is invisible on an e-ink panel until it has
// already cost you the hand.
namespace bj {

constexpr int RANKS = 13;
constexpr int SUITS = 4;
constexpr int DECKS = 6;
constexpr int DEPTH = RANKS * SUITS * DECKS;   // 312

// rank index -> display string, ace first
static inline const char* rankName(int r) {
  static const char* N[RANKS] = {"A","2","3","4","5","6","7","8","9","10","J","Q","K"};
  return N[r];
}
static inline int suitOf(int card) { return card % SUITS; }

// CRITICAL: the shoe holds DECKS copies, so a card index runs 0..311 while a
// rank only runs 0..12. Rank is (index/SUITS) MOD RANKS. Dropping the modulus
// makes a card from deck 2+ report rank 13..25, which silently corrupts every
// total and reads past the end of rankName(). This is why the soak test in
// tools/logic_test.cpp draws from the whole shoe rather than deck 0.
static inline int rankOf(int card) { return (card / SUITS) % RANKS; }

// Ace is index 0. Value with soft aces counted as 11 then reduced.
struct Hand {
  int cards[12];
  int n = 0;

  void add(int c) { if (n < 12) cards[n++] = c; }
  void clear() { n = 0; }

  // Returns total and sets *soft when at least one ace is still worth 11.
  int total(bool* soft = nullptr) const {
    int t = 0, aces = 0;
    for (int i = 0; i < n; i++) {
      int r = rankOf(cards[i]);
      if (r == 0) { aces++; t += 11; }
      else if (r >= 9) t += 10;        // 10, J, Q, K
      else t += r + 1;                 // 2..9 -> index 1..8
    }
    bool s = true;
    while (t > 21 && aces > 0) { t -= 10; aces--; }
    // true while an ace is still being counted as 11. A A = 12 is soft: one
    // ace is still 11, so taking another card cannot bust you.
    s = aces > 0 && t <= 21;
    if (soft) *soft = s;
    return t;
  }

  bool isBlackjack() const { return n == 2 && total() == 21; }
  bool busted() const { return total() > 21; }
};

// xorshift64* - tiny, deterministic, seedable. Deterministic matters: a reported
// bug can be replayed from its seed instead of being chased as "flaky".
struct Rng {
  uint64_t s;
  explicit Rng(uint64_t seed = 0x9E3779B97F4A7C15ull) : s(seed ? seed : 1) {}
  uint64_t next() {
    s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
    return s * 0x2545F4914F6CDD1Dull;
  }
  int below(int n) { return (int)(next() % (uint64_t)n); }
};

enum class Phase : uint8_t { BET, PLAYER, DEALER, RESULT };
enum class Outcome : uint8_t { NONE, PLAYER_WINS, DEALER_WINS, PUSH, PLAYER_BUST, DEALER_BUST, BLACKJACK };

struct Game {
  int shoe[DEPTH];
  int pos = DEPTH;
  int dealt = 0;              // cards since last shuffle, for penetration
  Rng rng;

  int bankroll = 100;
  int bet = 5;
  Hand player, dealer;
  Phase phase = Phase::BET;
  Outcome outcome = Outcome::NONE;

  explicit Game(uint64_t seed = 0) : rng(seed ? seed : 1) {}

  static constexpr int MIN_BET = 1;
  static constexpr int MAX_BET = 25;
  // Reshuffle once three quarters of the shoe is gone.
  static constexpr int PENETRATION = (DEPTH * 3) / 4;

  void shuffle() {
    for (int i = 0; i < DEPTH; i++) shoe[i] = i;
    for (int i = DEPTH - 1; i > 0; i--) {          // Fisher-Yates
      int j = rng.below(i + 1);
      int t = shoe[i]; shoe[i] = shoe[j]; shoe[j] = t;
    }
    pos = 0;
    dealt = 0;
  }

  int draw() {
    if (pos >= DEPTH || dealt >= PENETRATION) shuffle();
    dealt++;
    return shoe[pos++];
  }

  bool canBet(int b) const { return b >= MIN_BET && b <= MAX_BET && b <= bankroll; }
  void changeBet(int delta) {
    int b = bet + delta;
    if (b < MIN_BET) b = MIN_BET;
    if (b > MAX_BET) b = MAX_BET;
    if (b > bankroll) b = bankroll < MIN_BET ? MIN_BET : bankroll;
    bet = b;
  }

  // Start a hand. Bets down to whatever is left rather than refusing, so a
  // short bankroll can never wedge the UI on a screen with no legal move.
  bool deal() {
    if (bankroll < MIN_BET) return false;
    if (bet > bankroll) bet = bankroll;
    if (pos + 8 >= DEPTH) shuffle();
    player.clear(); dealer.clear();
    outcome = Outcome::NONE;
    player.add(draw());
    dealer.add(draw());
    player.add(draw());
    dealer.add(draw());

    bool ps = false, ds = false;
    player.total(&ps); dealer.total(&ds);
    // Natural pays immediately, unless the dealer's upcard can still tie it.
    if (player.isBlackjack() || dealer.isBlackjack()) { settle(); return true; }
    phase = Phase::PLAYER;
    return true;
  }

  void hit() {
    if (phase != Phase::PLAYER) return;
    player.add(draw());
    if (player.busted()) settle();
  }

  bool canDouble() const {
    return phase == Phase::PLAYER && player.n == 2 && bankroll >= bet * 2;
  }

  void dbl() {
    if (!canDouble()) return;
    bet *= 2;
    player.add(draw());          // exactly one card, then stand
    settle();
  }

  void stand() {
    if (phase != Phase::PLAYER) return;
    phase = Phase::DEALER;
    playDealer();
    settle();
  }

  // Dealer stands on all 17 including soft 17 (common rule; change to < 18 for
  // HIT SOFT 17 and the tests in tools/logic_test.cpp will still hold).
  void playDealer() {
    while (dealer.total() < 17) dealer.add(draw());
  }

  void settle() {
    int pt = player.total(), dt = dealer.total();
    bool pb = player.busted(), db = dealer.busted();
    int profit = 0;

    if (pb) { outcome = Outcome::PLAYER_BUST; profit = -bet; }
    else if (pt == 21 && player.n == 2 && !(dt == 21 && dealer.n == 2)) {
      outcome = Outcome::BLACKJACK; profit = (bet * 3) / 2;
    }
    else if (dt == 21 && dealer.n == 2 && !(pt == 21 && player.n == 2)) {
      outcome = Outcome::DEALER_WINS; profit = -bet;
    }
    else if (db) { outcome = Outcome::DEALER_BUST; profit = bet; }
    else if (pt > dt) { outcome = Outcome::PLAYER_WINS; profit = bet; }
    else if (pt < dt) { outcome = Outcome::DEALER_WINS; profit = -bet; }
    else outcome = Outcome::PUSH;

    bankroll += profit;
    if (bankroll < 0) bankroll = 0;
    phase = Phase::RESULT;
  }

  const char* outcomeText() const {
    switch (outcome) {
      case Outcome::BLACKJACK:    return "BLACKJACK";
      case Outcome::PLAYER_WINS:  return "YOU WIN";
      case Outcome::PLAYER_BUST:  return "BUST";
      case Outcome::DEALER_BUST:  return "DEALER BUSTS";
      case Outcome::DEALER_WINS:  return "DEALER WINS";
      case Outcome::PUSH:         return "PUSH";
      default:                    return "";
    }
  }
};

} // namespace bj
