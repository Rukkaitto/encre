#include "reader/screens.h"

#include "reader/screen_book_details.h"
#include "reader/screen_delete_confirm.h"
#include "reader/screen_item_actions.h"
#include "reader/screen_library.h"
#include "reader/screen_sd_missing.h"

namespace reader {

HomeViewModel demoHomeVm() {
  HomeViewModel vm;
  vm.title = "Middlemarch";
  vm.author = "George Eliot";
  vm.chapterLabel = "CH. 01 \xE2\x80\x94 MISS BROOKE";
  vm.percent = 6;
  vm.currentPage = 53;
  vm.pageCount = 890;
  vm.batteryPercent = 87;
  vm.hasCover = false;
  vm.menu = {{"LIBRARY", "12"}, {"SETTINGS", ""}};
  vm.focusedMenuIndex = -1;
  vm.hints = {"READ", "SELECT", "UP", "DOWN"};
  // Home binds no long press, so no slot shows a ring.
  vm.holds = {false, false, false, false};
  return vm;
}

std::vector<ScreenId> demoHomeTargets() { return {ScreenId::Library, ScreenId::Settings}; }

std::vector<LibraryItem> demoLibraryItems() {
  // The board's rows, in the board's order, with the board's own authors and
  // right-hand values. The folder's `childBooks` is 6 because the board says
  // `FOLDER - 6 BOOKS`, and it is also what makes the band read `12 BOOKS` over
  // six books beside it -- the count is derived here exactly as it is on a card.
  //
  // The names carry extensions and the titles do not, because that is the
  // relationship BookList::titleFor establishes and the sample must not be a
  // second answer to it. Dubliners' size is what Book details formats as
  // `0.4 MB`.
  auto book = [](const char* name, const char* title, const char* author, const char* progress,
                 uint32_t size) {
    LibraryItem item;
    item.entry = BookEntry{name, title, false, size};
    item.author = author;
    item.progress = progress;
    return item;
  };
  LibraryItem folder;
  folder.entry = BookEntry{"Classics", "Classics", true, 0};
  folder.childBooks = 6;

  // Book details' board describes ONE of these rows, Dubliners, so its extra
  // fields are set on that one and left blank on the rest -- which is also what a
  // real card looks like today, since nothing can fill them in. Its 416 KB is
  // what the details screen formats as the board's `0.4 MB`.
  auto dubliners = [](LibraryItem item) {
    item.details.author = "James Joyce";
    item.details.subtitle = "Fifteen stories \xC2\xB7 1914";
    item.details.progress = "31% \xC2\xB7 PAGE 78 OF 252";
    item.details.chapter = "ARABY";
    item.details.added = "AUG 14, 2026";
    return item;
  };

  return {folder,
          book("Middlemarch.epub", "Middlemarch", "GEORGE ELIOT", "6%", 1268 * 1024),
          book("Jane Eyre.epub", "Jane Eyre", "CHARLOTTE BRONT\xC3\x8B", "DONE", 902 * 1024),
          book("Walden.epub", "Walden", "HENRY DAVID THOREAU", "48%", 511 * 1024),
          book("Meditations.epub", "Meditations", "MARCUS AURELIUS", "NEW", 288 * 1024),
          dubliners(book("Dubliners.epub", "Dubliners", "JAMES JOYCE", "31%", 416 * 1024)),
          book("The Odyssey.epub", "The Odyssey", "HOMER \xC2\xB7 TR. BUTLER", "NEW",
               1704 * 1024)};
}

DemoScreenFactory::DemoScreenFactory(FileSystem& fs, std::string root)
    : fs_(&fs), root_(std::move(root)) {}

std::unique_ptr<Screen> DemoScreenFactory::create(ScreenId id) {
  switch (id) {
    case ScreenId::Library: {
      // Over the card when there is one, over the board's own content when there
      // is not. Both go through the real LibraryScreen, so a desktop render is
      // evidence about the device rather than about a second, similar screen.
      auto lib = fs_ != nullptr ? std::make_unique<LibraryScreen>(*fs_, root_)
                                : std::make_unique<LibraryScreen>(demoLibraryItems());
      lib->setVisibleRows(libraryVisibleRows_);
      library_ = lib.get();
      return lib;
    }
    case ScreenId::ItemActions:
      // Over the Library this factory built last, which is the one on the stack:
      // an overlay is only ever pushed BY a live Library. A null there is a
      // refused push, which leaves the stack alone rather than putting a hole in
      // it.
      if (library_ == nullptr) return nullptr;
      return std::make_unique<ItemActionsScreen>(*library_);
    case ScreenId::DeleteConfirm:
      if (library_ == nullptr) return nullptr;
      return std::make_unique<DeleteConfirmScreen>(*library_);
    case ScreenId::BookDetails:
      if (library_ == nullptr) return nullptr;
      return std::make_unique<BookDetailsScreen>(*library_);
    case ScreenId::Settings: {
      auto scr = std::make_unique<SettingsScreen>(settings_, settingsSink_);
      scr->setMetrics(settingsListH_, settingsRowH_, settingsHeaderH_);
      return scr;
    }
    case ScreenId::SdMissing:
      // Buildable through the factory, not only as a root, so the shell can
      // replace the stack with it if the card goes away later and the simulator
      // can render it. It takes no arguments: a missing card is a missing card.
      return std::make_unique<SdMissingScreen>();
    case ScreenId::Home:
      // The root is never rebuilt: popping to Home returns the original object,
      // with its focus intact.
      return nullptr;
  }
  return nullptr;
}

}  // namespace reader
