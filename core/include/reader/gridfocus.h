#pragma once
#include <initializer_list>

#include "reader/focus.h"

namespace reader {

// WHERE THE SELECTION IS IN A GRID, for the screens a list cannot describe.
//
// `Focus` is a single int and knows one axis. The password keyboard is four
// rows of ten characters over a function row of four -- 44 cells in five rows,
// the last of them RAGGED -- and moving around it is two axes with different
// rules: the front buttons change row, the side buttons move along one.
//
// COMPOSED, NOT REIMPLEMENTED, which is ScrollWindow's shape: that class is "a
// Focus plus the window around it, so the two concerns are separable", and this
// is a Focus plus the row geometry around it. What the Focus underneath owns is
// the index itself, the range clamp, the wrapping flag and set()'s
// restore-path semantics, so none of that is written a second time.
//
// WHAT IT DOES *NOT* INHERIT, stated because the obvious assumption is wrong:
// the per-axis STEP is this class's own. Focus::move wraps over one range from
// 0 to count, and a grid needs a wrap that stays inside a row and a second that
// walks the rows -- neither of which is a sub-range Focus can express. The
// held-clamps-where-a-press-wraps rule is therefore also re-stated here rather
// than inherited, and it is pinned on both axes in test_gridfocus.cpp.
//
// THE RAGGED ROW IS NOT A SPECIAL CASE. Widths are per row, so the 4-wide
// function row is simply a row that is 4 wide; there is no branch for it, and
// the arithmetic that clamps a column into a narrow row is the same arithmetic
// that does nothing in a wide one.
//
// NO GATE, DELIBERATELY. Focus has one because Settings skips its section
// headers; every cell of every keyboard layer is a live key, so a gated walk
// here would be untested capability. The second grid that needs one is the
// place to add it, and it means mirroring Focus's step-over-a-refusal walk on
// both axes rather than passing a pointer through.
class GridFocus {
 public:
  // Five rows is the keyboard; the cap exists so the widths can live inline
  // with no allocation, which is what lets a screen hold one by value.
  static constexpr int kMaxRows = 8;

  GridFocus() = default;

  // Row widths, top to bottom -- `{10, 10, 10, 10, 4}` for the keyboard.
  //
  // A ZERO-WIDTH ROW IS REFUSED and takes the whole grid with it (rows() and
  // count() both come back 0), rather than being skipped. A row nothing can
  // land in would make moveRow's wrap step over it silently and leave row()
  // ambiguous at its boundary -- an empty grid is a state a screen already has
  // to handle, and a grid with a hole in it is not.
  GridFocus(std::initializer_list<int> rowWidths);

  int rows() const { return rows_; }
  int count() const { return focus_.count(); }
  int rowWidth(int row) const;

  // The flat cell index, counting left to right and then down. This is what a
  // screen reports as its focus and what the session record stores, so it has
  // to name the same cell it came from -- pinned as a round trip over every
  // cell.
  int index() const { return focus_.index(); }

  // -1 on an empty grid, for the reason Focus returns -1 for an empty list:
  // there is no row 0 to be on.
  int row() const;
  int col() const;

  // Selects outright, CLAMPING into range -- Focus's own distinction, and for
  // its reason: this is the restore path, where a record names a cell and no
  // press implies it. A record naming a cell past the end means "as far as you
  // can go", where wrapping it would land the user somewhere unrelated.
  //
  // Both re-establish the remembered column (see moveRow), because selecting a
  // cell outright is as deliberate a statement of which column you want as
  // moving along a row is.
  bool set(int index);
  bool setCell(int row, int col);

  // Moves along the current row. Wraps inside THAT ROW -- off the end returns
  // to its own first cell and never to the next row, which is what the board
  // promises in as many words ("the side page buttons move along a row").
  //
  // `held` clamps instead, on Focus's argument: a held button that wraps has no
  // end and cycles for as long as it is down, which is a carousel rather than
  // scrolling.
  //
  // Returns whether anything moved, so a screen can answer Action::none()
  // rather than pay a ~520 ms repaint that draws an identical frame.
  bool moveCol(int delta, bool held = false);

  // Moves between rows, keeping the column. Wraps off the last row onto the
  // first unless `held`.
  //
  // THE COLUMN IS REMEMBERED RATHER THAN CARRIED. Landing in a narrower row
  // clamps the column to fit, and the column the user actually chose is kept,
  // so coming back out of the 4-wide function row returns to where they were.
  // Without it, walking down through a short row and back up silently moves the
  // key under your thumb.
  bool moveRow(int delta, bool held = false);

  // Whether each axis rolls off its end onto the other. ON by default, like
  // every list in this firmware -- see Focus::setWrapping for what enabling it
  // reversed. Stored on the Focus rather than here so there is one home for the
  // answer, even though the stepping that consults it is this class's.
  void setWrapping(bool on) { focus_.setWrapping(on); }
  bool wraps() const { return focus_.wraps(); }

 private:
  // The flat index of a row's first cell.
  int rowStart(int row) const;
  // One axis's arithmetic: `from + delta` over `span` positions, wrapping when
  // this grid wraps and `held` is false, clamping otherwise. Both axes call it,
  // so the wrap-versus-clamp rule cannot be spelled twice.
  int step(int from, int delta, int span, bool held) const;
  // Lands on a cell and reports whether it moved. Every mutator ends here, so
  // the flat index and the two axes cannot disagree.
  bool land(int row, int col);

  Focus focus_;
  int widths_[kMaxRows] = {};
  int rows_ = 0;
  // The column the user last chose, which survives passing through a row too
  // narrow to hold it.
  int desiredCol_ = 0;
};

}  // namespace reader
