#pragma once
#include <string_view>

namespace reader {
class Framebuffer;
class Font;

// Which colour glyph coverage paints. White exists so inverted text (a focused
// row, a filled action block) needs no scratch buffer: fill the rect black,
// then draw over it with Ink::White.
enum class Ink { Black, White };

// Which bit-plane of a 2-bit grey level this pass emits. The screen is drawn
// three times: Bw produces the base frame the panel paints first, then Lsb and
// Msb produce the two planes the controller combines into 4 levels. Opaque
// drawing (rules, fills, icons) has coverage 0 or 3 and so is identical in all
// three; only glyph edges differ.
enum class Plane { Bw, Lsb, Msb };

// Draws UTF-8 text with kerning; (x, baselineY) is the pen origin.
// `tracking` adds that many pixels after every glyph, for the letterspaced
// labels the design uses. Returns the advance width consumed.
int drawText(Framebuffer& fb, const Font& font, int x, int baselineY, std::string_view utf8,
             Ink ink = Ink::Black, int tracking = 0, Plane plane = Plane::Bw);
}  // namespace reader
