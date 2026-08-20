#pragma once
// The Home view-model the goldens are blessed against -- which is the same one the
// firmware and the simulator show, deliberately: a golden pinning content the
// device does not display would pass while the screen was wrong.
#include "reader/screens.h"

inline reader::HomeViewModel sampleHome() { return reader::demoHomeVm(); }
