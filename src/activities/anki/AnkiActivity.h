#pragma once
#include <string>

#include "activities/Activity.h"
#include "anki/AnkiDeck.h"

/**
 * Activity for reviewing Anki flashcards via AnkiConnect.
 *
 * Flow:
 *   WiFi selection → sync due cards → for each card:
 *     show front → user reveals back → user rates → submit → next card
 *   → Done screen (or Error screen on failure)
 *
 * Button layout:
 *   SHOWING_FRONT : [Exit] [     ] [     ] [Show Answer]
 *   SHOWING_BACK  : [Again] [Hard] [Good] [Easy]
 *   DONE / ERROR  : [Back] [     ] [     ] [           ]
 */
class AnkiActivity final : public Activity {
 public:
  explicit AnkiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AnkiReview", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

  // Keep display alive throughout the review session.
  bool preventAutoSleep() override { return true; }

 private:
  enum class State {
    WIFI_SELECTION,
    SYNCING,
    FETCHING_CARD,
    SHOWING_FRONT,
    SHOWING_BACK,
    SUBMITTING,
    DONE,
    ERROR
  };

  State state = State::WIFI_SELECTION;
  AnkiDeck deck;
  std::string errorMessage;

  void onWifiConnected();
  void syncCards();
  void fetchCurrentCard();
  void submitAnswer(uint8_t ease);

  // Helper: draw a single centred status line with the standard header.
  void renderStatusMessage(const char* message, bool bold = false) const;
};
