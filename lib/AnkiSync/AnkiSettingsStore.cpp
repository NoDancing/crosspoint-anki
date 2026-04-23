#include "AnkiSettingsStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>

AnkiSettingsStore AnkiSettingsStore::instance;

namespace {
constexpr char ANKI_SETTINGS_PATH[] = "/.crosspoint/anki.json";
}  // namespace

bool AnkiSettingsStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");

  FsFile file;
  if (!Storage.openFileForWrite("ANKI", ANKI_SETTINGS_PATH, file)) {
    LOG_ERR("ANKI", "Failed to open settings file for write");
    return false;
  }

  JsonDocument doc;
  doc["host"] = host;
  serializeJson(doc, file);
  file.close();

  LOG_DBG("ANKI", "Saved settings: host=%s", host.c_str());
  return true;
}

bool AnkiSettingsStore::loadFromFile() {
  if (!Storage.exists(ANKI_SETTINGS_PATH)) {
    LOG_DBG("ANKI", "No settings file found");
    return false;
  }

  const String json = Storage.readFile(ANKI_SETTINGS_PATH);
  if (json.isEmpty()) {
    LOG_ERR("ANKI", "Settings file is empty");
    return false;
  }

  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, json);
  if (err) {
    LOG_ERR("ANKI", "Failed to parse settings: %s", err.c_str());
    return false;
  }

  host = doc["host"].as<std::string>();
  LOG_DBG("ANKI", "Loaded settings: host=%s", host.c_str());
  return true;
}
