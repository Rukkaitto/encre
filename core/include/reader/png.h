#pragma once
namespace reader {
class Framebuffer;

// Desktop-only (guarded by READER_DESKTOP): write fb as an 8-bit grayscale
// PNG (0x00 black / 0xFF white); compare fb against a PNG on disk.
bool writePng(const Framebuffer& fb, const char* path);
bool comparePng(const Framebuffer& fb, const char* path, int* wOut = nullptr, int* hOut = nullptr);

// Compose two single-plane framebuffers into one 4-level greyscale image and
// write it. Level per pixel is (msb << 1) | lsb, mapped 0..3 -> white..black,
// so an anti-aliased render is inspectable and diffable on the desktop exactly
// as a 1-bit one is. The Bw base pass is not part of the composition: it is a
// thresholded duplicate of the same coverage and carries no extra information.
bool writeGrayPng(const Framebuffer& lsb, const Framebuffer& msb, const char* path);

// Why a comparison failed, and by how much. comparePng answers only yes/no,
// which makes a golden-test failure impossible to triage: a missing file, a
// resized framebuffer and a one-pixel drawing change all look identical.
struct PngDiff {
  enum class Status {
    kMatch,
    kDecodeFailed,   // file missing, unreadable, or not a decodable image
    kSizeMismatch,   // decoded fine, but dimensions differ from fb
    kPixelMismatch,  // same size, differing pixels
  };
  Status status = Status::kDecodeFailed;
  int width = 0, height = 0;      // dimensions of the decoded image, 0 if undecodable
  long diffPixels = 0;            // differing pixels, set for kPixelMismatch
  int firstDiffX = -1, firstDiffY = -1;  // first differing pixel in raster order
  bool ok() const { return status == Status::kMatch; }
};

PngDiff diffPng(const Framebuffer& fb, const char* path);

// The 4-level sibling of diffPng: composes lsb/msb the same way writeGrayPng
// does and diffs the result against the image on disk.
PngDiff diffGrayPng(const Framebuffer& lsb, const Framebuffer& msb, const char* path);

const char* pngDiffStatusName(PngDiff::Status status);
}  // namespace reader
