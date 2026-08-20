#pragma once
namespace reader {
class Framebuffer;
// Desktop-only (guarded by READER_DESKTOP): write fb as an 8-bit grayscale
// PNG (0x00 black / 0xFF white); compare fb against a PNG on disk.
bool writePng(const Framebuffer& fb, const char* path);
bool comparePng(const Framebuffer& fb, const char* path, int* wOut = nullptr, int* hOut = nullptr);
}  // namespace reader
