#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

#include "AnkiSyncClient.h"

/**
 * In-session review queue.
 *
 * Holds the list of due card IDs fetched during sync and the content of the
 * single card currently under review. Only one card's question/answer buffers
 * (~1 KB) are in RAM at a time; the ID list for 50 cards is ~400 bytes.
 */
class AnkiDeck {
 public:
  // Replace the current queue with newly fetched IDs (moves the vector).
  void loadIds(std::vector<uint64_t>&& ids);

  // True while there is a card at the current index.
  bool hasCurrent() const { return reviewIndex < cardIds.size(); }

  // 0-based index of the card currently under review.
  size_t currentIndex() const { return reviewIndex; }

  // Total cards in this session.
  size_t totalCount() const { return cardIds.size(); }

  // ID of the card at the current index. Caller must check hasCurrent() first.
  uint64_t currentCardId() const;

  // Store fetched content for the current card.
  void setCurrentCard(const AnkiCard& card) { currentCard = card; }

  // Read-only access to current card content.
  const AnkiCard& getCurrentCard() const { return currentCard; }

  // Advance to the next card. Returns true if another card exists, false if done.
  bool advance();

  // Rewind to the start of the queue without re-fetching IDs.
  void reset();

 private:
  std::vector<uint64_t> cardIds;
  size_t reviewIndex = 0;
  AnkiCard currentCard = {};
};
