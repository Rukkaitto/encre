#pragma once
#include <string_view>

namespace reader {
class Framebuffer;
class Font;

// Which colour glyph coverage paints. White exists so inverted text (a focused
// row, a filled action block) needs no scratch buffer: fill the rect black,
// then draw over it with Ink::White.
enum class Ink { Black, White };

// Draws UTF-8 text with kerning; (x, baselineY) is the pen origin.
// `tracking` adds that many pixels after every glyph, for the letterspaced
// labels the design uses. Returns the advance width consumed.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, int tracking = 0);
}  // namespace reader
