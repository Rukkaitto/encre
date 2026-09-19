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
  void renderBookEnd(Framebuffer& fb, const FontSet& fonts, const BookEndViewModel& vm,
                     Plane plane = Plane::Bw) override;
  void renderBatteryEmpty(Framebuffer& fb, const FontSet& fonts, const BatteryEmptyViewModel& vm,
                          Plane plane = Plane::Bw) override;
  void renderLibrary(Framebuffer& fb, const FontSet& fonts, const LibraryViewModel& vm,
                     Plane plane = Plane::Bw) override;
  void renderItemActions(Framebuffer& fb, const FontSet& fonts, const ItemActionsViewModel& vm,
                         Plane plane = Plane::Bw) override;
  void renderDeleteConfirm(Framebuffer& fb, const FontSet& fonts,
                           const DeleteConfirmViewModel& vm, Plane plane = Plane::Bw) override;
  void renderBookError(Framebuffer& fb, const FontSet& fonts, const BookErrorViewModel& vm,
                       Plane plane = Plane::Bw) override;
  void renderWifiSettings(Framebuffer& fb, const FontSet& fonts,
                          const WifiSettingsViewModel& vm, Plane plane = Plane::Bw) override;
  void renderWifiPicker(Framebuffer& fb, const FontSet& fonts, const WifiPickerViewModel& vm,
                        Plane plane = Plane::Bw) override;
  void renderTextEntry(Framebuffer& fb, const FontSet& fonts, const TextEntryViewModel& vm,
                       Plane plane = Plane::Bw) override;
  void renderWifiConnect(Framebuffer& fb, const FontSet& fonts, const WifiConnectViewModel& vm,
                         Plane plane = Plane::Bw) override;
  void renderWifiError(Framebuffer& fb, const FontSet& fonts, const WifiErrorViewModel& vm,
                       Plane plane = Plane::Bw) override;
  void renderWifiNetworkActions(Framebuffer& fb, const FontSet& fonts,
                                const WifiNetworkActionsViewModel& vm,
                                Plane plane = Plane::Bw) override;

  // --- Articles over wallabag (V1.1) -----------------------------------
  void renderArticles(Framebuffer& fb, const FontSet& fonts, const ArticlesViewModel& vm,
                      Plane plane = Plane::Bw) override;
  void renderArticleActions(Framebuffer& fb, const FontSet& fonts,
                            const ArticleActionsViewModel& vm, Plane plane = Plane::Bw) override;
  void renderArticleEnd(Framebuffer& fb, const FontSet& fonts, const ArticleEndViewModel& vm,
                        Plane plane = Plane::Bw) override;
  void renderWallabagAccount(Framebuffer& fb, const FontSet& fonts,
                             const WallabagAccountViewModel& vm, Plane plane = Plane::Bw) override;
  void renderWallabagConnecting(Framebuffer& fb, const FontSet& fonts,
                                const WallabagConnectingViewModel& vm,
                                Plane plane = Plane::Bw) override;
  void renderWallabagError(Framebuffer& fb, const FontSet& fonts, const WallabagErrorViewModel& vm,
                           Plane plane = Plane::Bw) override;
  int articlesVisibleRows(int panelH, int panelW, const FontSet& fonts,
                          std::string_view statusLine) const override;
  void renderBookDetails(Framebuffer& fb, const FontSet& fonts, const BookDetailsViewModel& vm,
                         Plane plane = Plane::Bw) override;
  int libraryVisibleRows(int panelH, const FontSet& fonts) const override;
  void renderReaderMenu(Framebuffer& fb, const FontSet& fonts, const ReaderMenuViewModel& vm,
                        Plane plane) override;
  void renderContents(Framebuffer& fb, const FontSet& fonts, const ContentsViewModel& vm,
                      Plane plane) override;
  void renderNames(Framebuffer& fb, const FontSet& fonts, const NamesViewModel& vm,
                   Plane plane) override;
  int contentsVisibleRows(int panelH, const FontSet& fonts) override;

  void settingsMetrics(int panelH, const FontSet& fonts, int& listH, int& rowH,
                       int& headerH) const override;
  void renderSleep(Framebuffer& fb, const FontSet& fonts, const SleepViewModel& vm,
                   Plane plane, CoverSource* cover) override;

  void renderSettings(Framebuffer& fb, const FontSet& fonts, const SettingsViewModel& vm,
                      Plane plane) override;
  void renderTypography(Framebuffer& fb, const FontSet& fonts, const GlyphSource* body,
                        const TypographyViewModel& vm, Plane plane) override;
  void readerMetrics(int panelW, int panelH, const FontSet& fonts, const GlyphSource& body,
                     const Settings& settings, PageMetrics& out) const override;
  void renderReader(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                    const GlyphSource* italic, const ReaderViewModel& vm, const Page& page,
                    Plane plane) override;
  void peekMetrics(int panelW, int panelH, const FontSet& fonts, const GlyphSource& body,
                   const Settings& settings, PageMetrics& out) const override;
  int peekVisibleLines(const FontSet& fonts, const GlyphSource& body,
                       const Settings& settings) const override;
  void renderPeek(Framebuffer& fb, const FontSet& fonts, const GlyphSource& body,
                  const GlyphSource* italic, const PeekViewModel& vm, const Page& page,
                  Plane plane) override;
};

}  // namespace reader
