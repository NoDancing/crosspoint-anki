#pragma once
#include <cstdint>
#include <string>
#include <vector>

/**
 * Card content fetched from AnkiConnect.
 * HTML has been stripped; answer contains only the back of the card
 * (content before <hr id=answer> is discarded).
 * Fixed-size char arrays avoid std::string heap churn in the review loop.
 */
struct AnkiCard {
  uint64_t id;
  char question[512];  // HTML-stripped front, null-terminated
  char answer[512];    // HTML-stripped back only, null-terminated
};

/**
 * Stateless HTTP client for AnkiConnect (http://<host>:8765).
 *
 * All methods are blocking and must be called from a WiFi-connected context.
 * Host is read from AnkiSettingsStore at call time.
 *
 * AnkiConnect API: https://foosoft.net/projects/anki-connect/
 */
class AnkiSyncClient {
 public:
  enum Error { OK = 0, NO_HOST, NETWORK_ERROR, SERVER_ERROR, JSON_ERROR };

  // Maximum due cards fetched per session. Caps RAM use and session length.
  static constexpr size_t MAX_DUE_CARDS = 50;

  /**
   * Fetch IDs of all due cards, capped at MAX_DUE_CARDS.
   */
  static Error findDueCards(std::vector<uint64_t>& outIds);

  /**
   * Fetch and strip HTML for a single card by ID.
   */
  static Error getCardInfo(uint64_t cardId, AnkiCard& outCard);

  /**
   * Submit a review answer.
   * ease: 1=Again  2=Hard  3=Good  4=Easy
   */
  static Error answerCard(uint64_t cardId, uint8_t ease);

  static const char* errorString(Error error);
};
