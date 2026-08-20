#pragma once
#include <string_view>

namespace reader {
class Framebuffer;
class Font;

// Draws UTF-8 text with kerning; (x, baselineY) is the pen origin.
// Returns the advance width consumed. Ink is black on the framebuffer.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8);
}  // namespace reader
