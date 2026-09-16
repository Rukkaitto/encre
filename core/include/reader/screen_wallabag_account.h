#pragma once
#include <string>

#include "reader/filesystem.h"
#include "reader/focus_screen.h"
#include "reader/settings.h"
#include "reader/viewmodel.h"

namespace reader {

// design/WallabagAccount.dc.html -- setup and status, reached from Settings'
// CONNECTIONS row. SettingsScreen's shape: a band, rows interleaved with a
// section header, a focus that skips what cannot act, and a note.
//
// HOME'S ARTICLES ROW IS THE DOOR TO THE LIST AND THIS IS THE DOOR TO THE
// ACCOUNT. Two doors to two different rooms, which is what the Settings board's
// own note says and why restoring that row was not a duplicate of Home's.
class WallabagAccountScreen : public FocusScreen {
 public:
  struct Facts {
    std::string username;
    int unread = 0;
    // The watermark's outcome, ALREADY COMPOSED -- `NEVER`, `NO NEW`, `3 NEW`,
    // `FAILED`. The same four values the Articles list's stamp draws, from the
    // same watermark, because two screens naming one fact differently is two
    // spellings of it.
    std::string lastSync;
    int keepOffline = 50;
    int pending = 0;
    // Whether /.reader/wallabag.json carries all five values. NOT "has this
    // device ever synced": an unconfigured device is one nobody has set up, and
    // a configured one that has never synced is a different and ordinary state.
    bool configured = false;
  };

  enum class Chosen { None, KeepOffline };

  explicit WallabagAccountScreen(Facts facts);
  // OVER A FileSystem -- the device. Every value on this screen describes the
  // CARD (the credentials, the article count, the queue, the watermark), which
  // is the whole reason it is declared `Restore::Ready`: a chip reset changes
  // none of them.
  WallabagAccountScreen(FileSystem& fs, const Settings& settings);

  // Read the card again. Called after anything that changes what this screen
  // states -- a sync, a remove-all, a keep-offline commit.
  bool refresh();

  ScreenId id() const override { return ScreenId::WallabagAccount; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const WallabagAccountViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }
  Chosen chosen() const { return chosen_; }

  // The value the last KeepOffline press cycled TO, which the shell commits and
  // prunes against. Read off this screen while it is still on top.
  int keepOffline() const { return facts_.keepOffline; }

 protected:
  // WHICH ROWS THE FOCUS MAY LAND ON, consulted per landing rather than
  // tabulated -- Settings' own mechanism, and Typography's before it. A row that
  // cannot act cannot mislead, where a row that focuses and then ignores Confirm
  // is the silent no-op this project has been bitten by twice.
  bool focusable(int index) const override;
  void syncVm() override;

 private:
  // The board's rows, in its order. An enum rather than comparing a label,
  // because a label is content and what Confirm does is not.
  enum Row { kAccount = 0, kUnread, kLastSync, kKeepOffline, kPending, kHeader, kRemove,
             kRowCount };

  // Build Facts from the card. Static, so the constructor can use it in its
  // member initialiser and `refresh` can use the same one -- two spellings of
  // "what does the card say" is two answers a screen could show.
  static Facts factsFrom(FileSystem& fs, const Settings& settings);

  Facts facts_;
  WallabagAccountViewModel vm_;
  FileSystem* fs_ = nullptr;
  Chosen chosen_ = Chosen::None;
};

}  // namespace reader
