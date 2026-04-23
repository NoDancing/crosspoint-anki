#include "AnkiActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include "AnkiSettingsStore.h"
#include "AnkiSyncClient.h"
#include "MappedInputManager.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

// ─── Lifecycle ────────────────────────────────────────────────────────────────

void AnkiActivity::onEnter() {
  Activity::onEnter();

  state = State::WIFI_SELECTION;
  errorMessage.clear();
  deck.reset();
  requestUpdate();

  if (WiFi.status() == WL_CONNECTED) {
    onWifiConnected();
    return;
  }

  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput),
                         [this](const ActivityResult& result) {
                           if (result.isCancelled) {
                             finish();
                           } else {
                             onWifiConnected();
                           }
                         });
}

void AnkiActivity::onExit() {
  Activity::onExit();

  WiFi.disconnect(false);
  delay(100);
  WiFi.mode(WIFI_OFF);
  delay(100);
}

// ─── Network flow ─────────────────────────────────────────────────────────────

void AnkiActivity::onWifiConnected() {
  {
    RenderLock lock(*this);
    state = State::SYNCING;
  }
  requestUpdate();
  syncCards();
}

void AnkiActivity::syncCards() {
  std::vector<uint64_t> ids;
  const AnkiSyncClient::Error err = AnkiSyncClient::findDueCards(ids);

  if (err != AnkiSyncClient::OK) {
    RenderLock lock(*this);
    state = State::ERROR;
    errorMessage = AnkiSyncClient::errorString(err);
    requestUpdate();
    return;
  }

  if (ids.empty()) {
    RenderLock lock(*this);
    state = State::DONE;
    requestUpdate();
    return;
  }

  deck.loadIds(std::move(ids));
  fetchCurrentCard();
}

void AnkiActivity::fetchCurrentCard() {
  {
    RenderLock lock(*this);
    state = State::FETCHING_CARD;
  }
  requestUpdate();

  AnkiCard card;
  const AnkiSyncClient::Error err = AnkiSyncClient::getCardInfo(deck.currentCardId(), card);

  if (err != AnkiSyncClient::OK) {
    RenderLock lock(*this);
    state = State::ERROR;
    errorMessage = AnkiSyncClient::errorString(err);
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    deck.setCurrentCard(card);
    state = State::SHOWING_FRONT;
  }
  requestUpdate();
}

void AnkiActivity::submitAnswer(const uint8_t ease) {
  {
    RenderLock lock(*this);
    state = State::SUBMITTING;
  }
  requestUpdate();

  const AnkiSyncClient::Error err = AnkiSyncClient::answerCard(deck.currentCardId(), ease);

  if (err != AnkiSyncClient::OK) {
    RenderLock lock(*this);
    state = State::ERROR;
    errorMessage = AnkiSyncClient::errorString(err);
    requestUpdate();
    return;
  }

  if (deck.advance()) {
    fetchCurrentCard();
  } else {
    RenderLock lock(*this);
    state = State::DONE;
    requestUpdate();
  }
}

// ─── Input ────────────────────────────────────────────────────────────────────

void AnkiActivity::loop() {
  switch (state) {
    case State::SHOWING_FRONT:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        finish();
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        {
          RenderLock lock(*this);
          state = State::SHOWING_BACK;
        }
        requestUpdate();
      }
      break;

    case State::SHOWING_BACK:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
        submitAnswer(1);  // Again
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Left)) {
        submitAnswer(2);  // Hard
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Right)) {
        submitAnswer(3);  // Good
      } else if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        submitAnswer(4);  // Easy
      }
      break;

    case State::DONE:
    case State::ERROR:
      if (mappedInput.wasPressed(MappedInputManager::Button::Back) ||
          mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
        finish();
      }
      break;

    default:
      break;
  }
}

// ─── Rendering ────────────────────────────────────────────────────────────────

void AnkiActivity::renderStatusMessage(const char* message, const bool bold) const {
  const int y = (renderer.getScreenHeight() - renderer.getLineHeight(UI_12_FONT_ID)) / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, y, message, true,
                            bold ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);
}

void AnkiActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_ANKI_REVIEW));

  switch (state) {
    case State::WIFI_SELECTION:
      // WifiSelectionActivity is the active sub-activity; this render path is
      // only hit briefly before/after it runs, so a blank screen is fine.
      break;

    case State::SYNCING:
      renderStatusMessage(tr(STR_ANKI_SYNCING));
      break;

    case State::FETCHING_CARD:
    case State::SUBMITTING:
      renderStatusMessage(tr(STR_ANKI_LOADING_CARD));
      break;

    case State::DONE: {
      renderStatusMessage(tr(STR_ANKI_DONE), true);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::ERROR: {
      renderStatusMessage(errorMessage.c_str(), true);
      const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
      GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      break;
    }

    case State::SHOWING_FRONT:
    case State::SHOWING_BACK: {
      // ── Sub-header: card progress ────────────────────────────────────────
      char progressBuf[16];
      snprintf(progressBuf, sizeof(progressBuf), "%zu/%zu",
               deck.currentIndex() + 1, deck.totalCount());
      GUI.drawSubHeader(renderer,
                        Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                        progressBuf);

      // ── Content area bounds ──────────────────────────────────────────────
      const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight
                             + metrics.verticalSpacing;
      const int contentBottom = pageHeight - metrics.buttonHintsHeight - metrics.verticalSpacing;
      const int contentWidth = pageWidth - metrics.contentSidePadding * 2;
      const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
      const int maxLines = (contentBottom - contentTop) / lineHeight;

      // ── Card text ────────────────────────────────────────────────────────
      const char* text = (state == State::SHOWING_FRONT)
                             ? deck.getCurrentCard().question
                             : deck.getCurrentCard().answer;

      const auto lines = renderer.wrappedText(UI_12_FONT_ID, text, contentWidth, maxLines);
      int y = contentTop;
      for (const auto& line : lines) {
        renderer.drawText(UI_12_FONT_ID, metrics.contentSidePadding, y, line.c_str());
        y += lineHeight;
      }

      // ── Button hints ─────────────────────────────────────────────────────
      if (state == State::SHOWING_FRONT) {
        const auto labels = mappedInput.mapLabels(tr(STR_EXIT), "", "", tr(STR_ANKI_SHOW_ANSWER));
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      } else {
        const auto labels = mappedInput.mapLabels(
            tr(STR_ANKI_AGAIN), tr(STR_ANKI_HARD), tr(STR_ANKI_GOOD), tr(STR_ANKI_EASY));
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      break;
    }
  }

  renderer.displayBuffer();
}
