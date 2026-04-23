#pragma once
#include <string>

/**
 * Singleton for persisting AnkiConnect host configuration on the SD card.
 * Stored as JSON at /.crosspoint/anki.json
 *
 * Only compiled and used when ANKI_ENABLED is defined.
 */
class AnkiSettingsStore {
 private:
  static AnkiSettingsStore instance;
  std::string host;  // IP or hostname only — no protocol, no port (e.g. "192.168.1.100")

  AnkiSettingsStore() = default;

 public:
  AnkiSettingsStore(const AnkiSettingsStore&) = delete;
  AnkiSettingsStore& operator=(const AnkiSettingsStore&) = delete;

  static AnkiSettingsStore& getInstance() { return instance; }

  bool saveToFile() const;
  bool loadFromFile();

  void setHost(const std::string& h) { host = h; }
  const std::string& getHost() const { return host; }
  bool hasHost() const { return !host.empty(); }
};

#define ANKI_STORE AnkiSettingsStore::getInstance()
