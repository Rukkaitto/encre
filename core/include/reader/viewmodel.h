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
  std::vector<MenuEntry> menu;
  int focusedMenuIndex = -1;                 // -1 = Continue block focused
  std::array<std::string, 4> hints{};        // Back, Confirm, Up, Down slots
};

}  // namespace reader
