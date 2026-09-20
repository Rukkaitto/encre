#pragma once
// The SDK's own compatibility shim, reproduced: the driver was rebuilt as
// freeink::FreeInkDisplay and this alias preserves the include path and the type
// name. Kept as two files for the same reason the real one is -- main.cpp includes
// <EInkDisplay.h> and names `EInkDisplay`.
#include "FreeInkDisplay.h"

using EInkDisplay = freeink::FreeInkDisplay;
