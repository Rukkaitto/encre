#pragma once
// The Home view-model the goldens are blessed against. Shared so the plain-Home
// golden, the focused-Home golden and the components tests cannot drift apart:
// two copies of this content would mean two goldens that disagree about what
// Home contains while both passing.
#include "reader/viewmodel.h"

inline reader::HomeViewModel sampleHome() {
  reader::HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 — MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  return vm;
}
