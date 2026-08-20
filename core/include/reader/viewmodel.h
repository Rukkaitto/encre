#pragma once
#include <array>
#include <string>
#include <vector>

namespace reader {

struct MenuEntry {
  std::string label;
  std::string value;
};

// Semantic content + interaction state only. No geometry, no style.
struct HomeViewModel {
  std::string title;
  std::string author;
  std::string chapterLabel;
  int percent = 0;
  int currentPage = 0;
  int pageCount = 0;
  int batteryPercent = 0;
  // Cover art is not decoded yet (Phase 3 owns EPUB images), so the theme draws
  // a dithered placeholder carrying the title. This flag says whether a real
  // cover exists, so the placeholder can be replaced without a view-model change.
  bool hasCover = false;
  std::vector<MenuEntry> menu;
  int focusedMenuIndex = -1;                 // -1 = Continue block focused
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
  // Which of those four buttons also has a long-press action. The theme draws a
  // hollow ring on the slot (design 662557d) and the screen builds its
  // long-press mask from the same array, so the affordance and the behaviour
  // cannot drift apart -- a ring always means a hold is bound, and a bound hold
  // always shows a ring.
  std::array<bool, 4> holds{};
};

}  // namespace reader
