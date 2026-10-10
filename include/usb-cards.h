#pragma once
#include <ArduinoJson.h>
#include <array>
#include <cstdint>
#include <cstring>

struct UsbCard {
  char id[37] = {};
  char title[49] = "Waiting for USB";
  char subtitle[49] = {};
  std::uint8_t status = 0;
};

// Complete frames are validated before replacing any visible card. Each tap
// carries the actual displayed UUID, never a slot that can be reassigned.
class UsbCards {
 public:
  std::array<UsbCard, 6> cards{};
  char session[33] = {};
  std::uint32_t lastFrame = 0;
  bool received = false;
  unsigned page = 1;
  unsigned pages = 1;
  bool fresh(std::uint32_t now) const { return received && now - lastFrame < 8000U; }
  static bool identifier(const char* s, unsigned length, bool hyphens) {
    if (!s || std::strlen(s) != length) return false;
    for (unsigned i = 0; i < length; ++i) {
      if (hyphens && (i == 8 || i == 13 || i == 18 || i == 23)) {
        if (s[i] != '-') return false;
      } else if (!((s[i] >= '0' && s[i] <= '9') || (s[i] >= 'a' && s[i] <= 'f'))) return false;
    }
    return true;
  }
  bool accept(const char* payload, std::uint32_t now) {
    StaticJsonDocument<3072> doc;
    if (deserializeJson(doc, payload)) return false;
    const char* nextSession = doc["session"];
    JsonArrayConst incoming = doc["cards"].as<JsonArrayConst>();
    if (!identifier(nextSession, 32, false) || incoming.size() != 6) return false;
    const unsigned nextPage = doc["page"] | 1U;
    const unsigned nextPages = doc["pages"] | 1U;
    if (!nextPage || nextPage > nextPages || nextPages > 999) return false;
    std::array<UsbCard, 6> next{};
    for (unsigned i = 0; i < 6; ++i) {
      const char* id = incoming[i]["id"];
      const char* title = incoming[i]["title"];
      const char* subtitle = incoming[i]["subtitle"] | "";
      if (std::strlen(subtitle) > 48) return false;
      for (const char* p = subtitle; *p; ++p) if (*p < 32 || *p > 126) return false;
      if (!id || (*id && !identifier(id, 36, true)) || !title || std::strlen(title) > 48 ||
          !incoming[i]["status"].is<unsigned>() || incoming[i]["status"].as<unsigned>() > 5) return false;
      for (const char* p = title; *p; ++p) if (*p < 32 || *p > 126) return false;
      for (unsigned j = 0; j < i; ++j) if (*id && std::strcmp(next[j].id, id) == 0) return false;
      std::strcpy(next[i].id, id);
      std::strcpy(next[i].title, title);
      std::strcpy(next[i].subtitle, subtitle);
      next[i].status = incoming[i]["status"];
    }
    cards = next;
    page = nextPage;
    pages = nextPages;
    std::strcpy(session, nextSession);
    lastFrame = now;
    received = true;
    return true;
  }
};
