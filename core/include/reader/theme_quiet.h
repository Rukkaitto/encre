#pragma once
#include "reader/fontset.h"
#include "reader/theme.h"

namespace reader {

// B2 "Quiet": Space Grotesk chrome, a 2px-ruled header band, hairline rows,
// black fill for focus. Layout follows the design canvas and derives every
// horizontal position from the framebuffer, so the same code composes correctly
// on the X4's 480x800 and the X3's 528x792.
class QuietTheme : public Theme {
 public:
  void renderHome(Framebuffer& fb, const FontSet& fonts, const HomeViewModel& vm,
                  Plane plane = Plane::Bw) override;
  void renderSdMissing(Framebuffer& fb, const FontSet& fonts, const SdMissingViewModel& vm,
                       Plane plane = Plane::Bw) override;
  void renderLibrary(Framebuffer& fb, const FontSet& fonts, const LibraryViewModel& vm,
                     Plane plane = Plane::Bw) override;
  void renderItemActions(Framebuffer& fb, const FontSet& fonts, const ItemActionsViewModel& vm,
                         Plane plane = Plane::Bw) override;
  void renderDeleteConfirm(Framebuffer& fb, const FontSet& fonts,
                           const DeleteConfirmViewModel& vm, Plane plane = Plane::Bw) override;
  void renderBookDetails(Framebuffer& fb, const FontSet& fonts, const BookDetailsViewModel& vm,
                         Plane plane = Plane::Bw) override;
  int libraryVisibleRows(int panelH, const FontSet& fonts) const override;
  void renderReaderMenu(Framebuffer& fb, const FontSet& fonts, const ReaderMenuViewModel& vm,
                        Plane plane) override;
  void renderContents(Framebuffer& fb, const FontSet& fonts, const ContentsViewModel& vm,
                      Plane plane) override;
  int contentsVisibleRows(int panelH, const FontSet& fonts) override;

  void settingsMetrics(int panelH, const FontSet& fonts, int& listH, int& rowH,
                       int& headerH) const override;
  void renderSleep(Framebuffer& fb, const FontSet& fonts, const SleepViewModel& vm,
                   Plane plane) override;

  void renderSettings(Framebuffer& fb, const FontSet& fonts, const SettingsViewModel& vm,
                      Plane plane) override;
  void readerMetrics(int panelW, int panelH, const FontSet& fonts, const GlyphSource& body,
                     PageMetrics& out) const override;
  void renderReader(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                    const GlyphSource* italic, const ReaderViewModel& vm, const Page& page,
                    Plane plane) override;

};

}  // namespace reader
