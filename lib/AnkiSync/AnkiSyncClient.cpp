#include "AnkiSyncClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFiClient.h>
#include <strings.h>  // strncasecmp

#include <cstring>

#include "AnkiSettingsStore.h"

namespace {

constexpr uint16_t ANKI_PORT = 8765;

// Max chars written into question[]/answer[], excluding the null terminator.
constexpr size_t FIELD_MAX = sizeof(AnkiCard::question) - 1;

// ─── URL builder ─────────────────────────────────────────────────────────────

bool buildUrl(char* outUrl, size_t outSize) {
  const std::string& host = ANKI_STORE.getHost();
  if (host.empty()) return false;
  snprintf(outUrl, outSize, "http://%s:%u", host.c_str(), ANKI_PORT);
  return true;
}

// ─── HTTP transport ───────────────────────────────────────────────────────────

// POST body to AnkiConnect, populate outResponse with the reply body.
AnkiSyncClient::Error postRequest(const char* body, std::string& outResponse) {
  char url[64];
  if (!buildUrl(url, sizeof(url))) {
    LOG_ERR("ANKI", "No host configured");
    return AnkiSyncClient::NO_HOST;
  }

  WiFiClient client;
  HTTPClient http;
  http.begin(client, url);
  http.addHeader("Content-Type", "application/json");

  const int httpCode = http.POST(body);
  if (httpCode == HTTP_CODE_OK) {
    outResponse = http.getString().c_str();
    http.end();
    return AnkiSyncClient::OK;
  }

  http.end();
  LOG_ERR("ANKI", "POST to %s failed: %d", url, httpCode);
  return (httpCode < 0) ? AnkiSyncClient::NETWORK_ERROR : AnkiSyncClient::SERVER_ERROR;
}

// ─── HTML stripper ────────────────────────────────────────────────────────────

// Copy src into dst (null-terminated, at most dstMax chars written):
//   • Skips the text content of <style> and <script> blocks entirely.
//   • Replaces <img...> tags with "[image]".
//   • Strips all other tags, keeping their text content.
//   • Decodes &amp; &lt; &gt; &nbsp;
//   • Collapses all whitespace runs to a single space.
void stripHtml(const char* src, char* dst, size_t dstMax) {
  if (!src) {
    dst[0] = '\0';
    return;
  }

  size_t j = 0;
  bool inTag = false;
  bool skipBlock = false;  // inside <style>…</style> or <script>…</script>
  bool lastWasSpace = false;

  for (size_t i = 0; src[i] != '\0' && j < dstMax; i++) {
    const char c = src[i];

    // ── Inside a skipped block (style/script content) ──────────────────────
    if (skipBlock) {
      if (c == '<' && src[i + 1] == '/') {
        if (strncasecmp(src + i, "</style>", 8) == 0 || strncasecmp(src + i, "</script>", 9) == 0) {
          skipBlock = false;
          inTag = true;  // consume the closing tag
        }
      }
      continue;
    }

    // ── Inside a tag ────────────────────────────────────────────────────────
    if (inTag) {
      if (c == '>') inTag = false;
      continue;
    }

    // ── Opening '<' ─────────────────────────────────────────────────────────
    if (c == '<') {
      if (strncasecmp(src + i, "<style", 6) == 0 || strncasecmp(src + i, "<script", 7) == 0) {
        skipBlock = true;
        inTag = true;
        continue;
      }
      if (strncasecmp(src + i, "<img", 4) == 0) {
        static constexpr char PLACEHOLDER[] = "[image]";
        static constexpr size_t PLACEHOLDER_LEN = sizeof(PLACEHOLDER) - 1;
        if (j + PLACEHOLDER_LEN <= dstMax) {
          memcpy(dst + j, PLACEHOLDER, PLACEHOLDER_LEN);
          j += PLACEHOLDER_LEN;
          lastWasSpace = false;
        }
      }
      inTag = true;
      continue;
    }

    // ── HTML entities ────────────────────────────────────────────────────────
    if (c == '&') {
      if (strncmp(src + i, "&amp;",  5) == 0) { dst[j++] = '&'; i += 4; lastWasSpace = false; continue; }
      if (strncmp(src + i, "&lt;",   4) == 0) { dst[j++] = '<'; i += 3; lastWasSpace = false; continue; }
      if (strncmp(src + i, "&gt;",   4) == 0) { dst[j++] = '>'; i += 3; lastWasSpace = false; continue; }
      if (strncmp(src + i, "&nbsp;", 6) == 0) { dst[j++] = ' '; i += 5; lastWasSpace = true;  continue; }
    }

    // ── Whitespace normalisation ─────────────────────────────────────────────
    if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
      if (!lastWasSpace && j > 0) {
        dst[j++] = ' ';
        lastWasSpace = true;
      }
      continue;
    }

    dst[j++] = c;
    lastWasSpace = false;
  }

  // Trim trailing space
  while (j > 0 && dst[j - 1] == ' ') j--;
  dst[j] = '\0';
}

// ─── Answer splitter ──────────────────────────────────────────────────────────

// AnkiConnect's `answer` field contains: <front HTML> <hr id=answer> <back HTML>
// Returns a pointer to the start of the back content, or the full string if
// the separator is not found.
const char* backContent(const char* answer) {
  const char* sep = strstr(answer, "<hr id=answer>");
  if (!sep) sep = strstr(answer, "<hr id=\"answer\">");
  if (!sep) return answer;
  const char* gt = strchr(sep, '>');
  return gt ? gt + 1 : answer;
}

}  // namespace

// ─── Public API ───────────────────────────────────────────────────────────────

AnkiSyncClient::Error AnkiSyncClient::findDueCards(std::vector<uint64_t>& outIds) {
  JsonDocument req;
  req["action"] = "findCards";
  req["version"] = 6;
  req["params"]["query"] = "is:due";

  std::string body;
  serializeJson(req, body);

  std::string response;
  const Error err = postRequest(body.c_str(), response);
  if (err != OK) return err;

  JsonDocument resp;
  const DeserializationError parseErr = deserializeJson(resp, response);
  if (parseErr) {
    LOG_ERR("ANKI", "findDueCards parse failed: %s", parseErr.c_str());
    return JSON_ERROR;
  }

  if (!resp["error"].isNull()) {
    LOG_ERR("ANKI", "AnkiConnect error: %s", resp["error"].as<const char*>());
    return SERVER_ERROR;
  }

  const JsonArray ids = resp["result"].as<JsonArray>();
  const size_t total = ids.size();
  const size_t count = total < MAX_DUE_CARDS ? total : MAX_DUE_CARDS;

  outIds.clear();
  outIds.reserve(count);
  for (size_t i = 0; i < count; i++) {
    outIds.push_back(ids[i].as<uint64_t>());
  }

  LOG_INF("ANKI", "Due cards: %u total, %u fetched", (unsigned)total, (unsigned)count);
  return OK;
}

AnkiSyncClient::Error AnkiSyncClient::getCardInfo(const uint64_t cardId, AnkiCard& outCard) {
  JsonDocument req;
  req["action"] = "cardsInfo";
  req["version"] = 6;
  req["params"]["cards"][0] = cardId;

  std::string body;
  serializeJson(req, body);

  std::string response;
  const Error err = postRequest(body.c_str(), response);
  if (err != OK) return err;

  JsonDocument resp;
  const DeserializationError parseErr = deserializeJson(resp, response);
  if (parseErr) {
    LOG_ERR("ANKI", "getCardInfo parse failed: %s", parseErr.c_str());
    return JSON_ERROR;
  }

  if (!resp["error"].isNull()) {
    LOG_ERR("ANKI", "AnkiConnect error: %s", resp["error"].as<const char*>());
    return SERVER_ERROR;
  }

  const JsonArray cards = resp["result"].as<JsonArray>();
  if (cards.size() == 0) {
    LOG_ERR("ANKI", "No data returned for card %llu", (unsigned long long)cardId);
    return SERVER_ERROR;
  }

  const JsonObject card = cards[0];
  outCard.id = cardId;

  stripHtml(card["question"].as<const char*>(), outCard.question, FIELD_MAX);
  stripHtml(backContent(card["answer"].as<const char*>() ? card["answer"].as<const char*>() : ""),
            outCard.answer, FIELD_MAX);

  LOG_DBG("ANKI", "Card %llu: Q[%u] A[%u]",
          (unsigned long long)cardId, (unsigned)strlen(outCard.question), (unsigned)strlen(outCard.answer));
  return OK;
}

AnkiSyncClient::Error AnkiSyncClient::answerCard(const uint64_t cardId, const uint8_t ease) {
  JsonDocument req;
  req["action"] = "answerCards";
  req["version"] = 6;
  req["params"]["answers"][0]["cardId"] = cardId;
  req["params"]["answers"][0]["ease"]   = ease;

  std::string body;
  serializeJson(req, body);

  std::string response;
  const Error err = postRequest(body.c_str(), response);
  if (err != OK) return err;

  JsonDocument resp;
  const DeserializationError parseErr = deserializeJson(resp, response);
  if (parseErr) {
    LOG_ERR("ANKI", "answerCard parse failed: %s", parseErr.c_str());
    return JSON_ERROR;
  }

  if (!resp["error"].isNull()) {
    LOG_ERR("ANKI", "AnkiConnect error: %s", resp["error"].as<const char*>());
    return SERVER_ERROR;
  }

  LOG_DBG("ANKI", "Answered card %llu ease=%d", (unsigned long long)cardId, ease);
  return OK;
}

const char* AnkiSyncClient::errorString(const Error error) {
  switch (error) {
    case OK:            return "Success";
    case NO_HOST:       return "No AnkiConnect host configured";
    case NETWORK_ERROR: return "Network error — is Anki open?";
    case SERVER_ERROR:  return "AnkiConnect error — check Anki is running";
    case JSON_ERROR:    return "Invalid response from AnkiConnect";
    default:            return "Unknown error";
  }
}
