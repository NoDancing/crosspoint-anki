#include "AnkiDeck.h"

#include <Logging.h>
#include <cassert>

void AnkiDeck::loadIds(std::vector<uint64_t>&& ids) {
  cardIds = std::move(ids);
  reviewIndex = 0;
  currentCard = {};
  LOG_DBG("ANKI", "Deck loaded: %u cards", (unsigned)cardIds.size());
}

uint64_t AnkiDeck::currentCardId() const {
  assert(hasCurrent());
  return cardIds[reviewIndex];
}

bool AnkiDeck::advance() {
  if (hasCurrent()) {
    reviewIndex++;
    currentCard = {};
  }
  return hasCurrent();
}

void AnkiDeck::reset() {
  reviewIndex = 0;
  currentCard = {};
}
