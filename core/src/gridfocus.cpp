#include "reader/gridfocus.h"

namespace reader {
namespace {

// A modulo that is never negative, which C++'s % is not: -7 % 5 is -2 here and
// the wrap needs 3. Both axes take deltas that can be large and negative,
// because a held button delivers a distance rather than a press.
int wrapInto(int v, int span) {
  if (span <= 0) return 0;
  const int m = v % span;
  return m < 0 ? m + span : m;
}

int clampInto(int v, int span) {
  if (span <= 0) return 0;
  if (v < 0) return 0;
  if (v >= span) return span - 1;
  return v;
}

}  // namespace

GridFocus::GridFocus(std::initializer_list<int> rowWidths) {
  const int n = static_cast<int>(rowWidths.size());
  if (n <= 0 || n > kMaxRows) return;
  int total = 0;
  int i = 0;
  for (int w : rowWidths) {
    // A row nothing can land in is refused, and it takes the grid with it --
    // see the header. Checked before anything is stored, so a refused grid is
    // indistinguishable from a default-constructed one.
    if (w <= 0) return;
    widths_[i++] = w;
    total += w;
  }
  rows_ = n;
  focus_ = Focus(total);
  desiredCol_ = 0;
}

int GridFocus::rowWidth(int row) const {
  if (row < 0 || row >= rows_) return 0;
  return widths_[row];
}

int GridFocus::rowStart(int row) const {
  int start = 0;
  for (int r = 0; r < row && r < rows_; ++r) start += widths_[r];
  return start;
}

int GridFocus::row() const {
  const int idx = focus_.index();
  if (idx < 0) return -1;
  int remaining = idx;
  for (int r = 0; r < rows_; ++r) {
    if (remaining < widths_[r]) return r;
    remaining -= widths_[r];
  }
  return rows_ - 1;
}

int GridFocus::col() const {
  const int r = row();
  if (r < 0) return -1;
  return focus_.index() - rowStart(r);
}

int GridFocus::step(int from, int delta, int span, bool held) const {
  if (span <= 0) return 0;
  const int target = from + delta;
  // A held move clamps even on a wrapping grid. Focus makes the same choice on
  // its one axis and states why; a grid has to make it twice.
  return (focus_.wraps() && !held) ? wrapInto(target, span) : clampInto(target, span);
}

bool GridFocus::land(int row, int col) {
  if (rows_ <= 0) return false;
  const int r = clampInto(row, rows_);
  const int c = clampInto(col, widths_[r]);
  // Focus owns the landing: the clamp into the flat range, the index itself and
  // the did-anything-move answer all come from it rather than being recomputed.
  return focus_.set(rowStart(r) + c);
}

bool GridFocus::set(int index) {
  const bool moved = focus_.set(index);
  // Read the column back rather than deriving it from `index`, which may have
  // been clamped: the remembered column must be where the focus actually is.
  const int c = col();
  if (c >= 0) desiredCol_ = c;
  return moved;
}

bool GridFocus::setCell(int row, int col) {
  const bool moved = land(row, col);
  const int c = this->col();
  if (c >= 0) desiredCol_ = c;
  return moved;
}

bool GridFocus::moveCol(int delta, bool held) {
  const int r = row();
  if (r < 0) return false;
  const int target = step(col(), delta, widths_[r], held);
  const bool moved = land(r, target);
  // Moving along a row IS the user choosing a column, so it replaces the
  // memory rather than being overridden by it.
  desiredCol_ = col();
  return moved;
}

bool GridFocus::moveRow(int delta, bool held) {
  const int r = row();
  if (r < 0) return false;
  const int target = step(r, delta, rows_, held);
  // desiredCol_ deliberately survives: land() clamps it into the target row,
  // and the next vertical move reads the original again.
  return land(target, desiredCol_);
}

}  // namespace reader
