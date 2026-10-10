#include "usb-cards.h"
#include <cassert>
#include <string>
#include <iostream>
int main() {
  UsbCards cards;
  assert(!cards.fresh(0));
  const std::string card = R"({"id":"11111111-1111-4111-8111-111111111111","title":"Atlas","subtitle":"Build dashboard","status":2})";
  const std::string empty = R"({"id":"","title":"No chat","status":0})";
  const std::string start = R"({"session":"0123456789abcdef0123456789abcdef","cards":[)";
  const std::string valid = start + card + "," + empty + "," + empty + "," + empty + "," + empty + "," + empty + "]}";
  assert(cards.accept(valid.c_str(), 100));
  assert(cards.fresh(8099));
  assert(!cards.fresh(8100));
  assert(std::string(cards.cards[0].title) == "Atlas");
  assert(std::string(cards.cards[0].subtitle) == "Build dashboard");
  auto oversized = valid;
  oversized.replace(oversized.find("Build dashboard"), 15, std::string(49, 'x'));
  assert(!cards.accept(oversized.c_str(), 200));
  assert(std::string(cards.cards[0].subtitle) == "Build dashboard");
  assert(cards.cards[1].subtitle[0] == 0);
  assert(!cards.accept("{}", 200));
  assert(cards.lastFrame == 100);
  assert(!cards.accept((start + card + "," + card + "," + empty + "," + empty + "," + empty + "," + empty + "]}").c_str(), 200));
  auto bad = valid; bad.replace(bad.find("\"status\":2"), 10, "\"status\":9");
  assert(!cards.accept(bad.c_str(), 200));
  assert(cards.cards[0].status == 2);
  // Unsigned millisecond wraparound remains safe.
  assert(cards.accept(valid.c_str(), 0xFFFFFF00U));
  assert(cards.fresh(0x00000100U));
  UsbCards paged;
  auto frame = valid;
  frame.insert(frame.find("\"cards\""), "\"page\":2,\"pages\":10,");
  assert(paged.accept(frame.c_str(), 100));
  assert(paged.page == 2 && paged.pages == 10);
  frame.replace(frame.find("\"page\":2"), 8, "\"page\":0");
  assert(!paged.accept(frame.c_str(), 200));
  assert(paged.page == 2 && paged.lastFrame == 100);
  std::cout << "USB frame validation, atomicity, duplicate IDs and expiry passed\n";
}
