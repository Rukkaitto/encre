#pragma once
#include <string>
#include <vector>

#include "reader/focus_screen.h"
#include "reader/viewmodel.h"

namespace reader {

// design/ArticleEnd.dc.html -- what an article's last page turns into, where a
// book's turns into BookEnd.
//
// THE ONLY OTHER SCREEN A PAGE TURN OPENS RATHER THAN A PRESS, which is BookEnd's
// own property and carries its consequence: it must be reachable with no button
// bound to it, so nothing here may depend on a gesture having chosen it.
class ArticleEndScreen : public FocusScreen {
 public:
  struct Facts {
    int id = 0;
    std::string title;
    std::string domain;
    int readingMinutes = 0;
    bool starred = false;
    // UNREAD ARTICLES STILL ON THE CARD AFTER THIS ONE. A count of FILES, never
    // a figure from the server -- design/ArticleEnd.dc.html's note has the
    // argument: the server's unread count includes everything this device has
    // not fetched and everything a phone archived an hour ago, and the panel
    // would be claiming one while counting the other.
    int unreadRemaining = 0;
    // Whether `NEXT ARTICLE` is drawn AT ALL. WifiError's rule -- the slab list
    // IS the shape -- rather than an inert slab, which is the works-only-
    // sometimes trap this project has shipped twice.
    //
    // NOT DERIVED FROM `unreadRemaining > 0`, and the difference is real: an
    // unread article may remain that this one is not followed BY, because the
    // shell's "next" walks the list in order from here rather than picking any
    // unread row. Deriving it would draw a slab that lands nowhere.
    bool hasNext = false;
  };

  enum class Chosen { None, Archive, Star, NextArticle, BackToList };

  explicit ArticleEndScreen(Facts facts);

  ScreenId id() const override { return ScreenId::ArticleEnd; }
  Action onGesture(const GestureEvent& g) override;
  void render(Framebuffer& fb, const FontSet& fonts, Theme& theme, Plane plane) const override;
  const ArticleEndViewModel& vm() const { return vm_; }
  const Facts& facts() const { return facts_; }
  Chosen chosen() const { return chosen_; }

 private:
  // WHAT THE FOCUSED SLAB MEANS, resolved through the slab LIST rather than
  // through a fixed index: with `NEXT ARTICLE` absent, `BACK TO LIST` is slab 2
  // rather than slab 3, and an enum of positions would name the wrong one.
  Chosen chosenAt(int slab) const;

  void syncVm() override;
  Facts facts_;
  ArticleEndViewModel vm_;
  std::vector<Chosen> slabs_;
  Chosen chosen_ = Chosen::None;
};

}  // namespace reader
