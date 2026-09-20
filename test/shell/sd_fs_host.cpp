// THE CARD'S DESKTOP TWIN, at the reader::FileSystem seam SdFileSystem already
// satisfies -- so the real sd_fs.cpp (773 lines) is NOT compiled and SdFat is not
// faked underneath it. That is the rule at the top of fake_arduino/harness_state.h,
// and it is the difference between ~1,000 lines of scaffolding and ~2,000.
//
// WHAT IS LOST, SAID PLAINLY. The listing cache's eviction, the probe-target
// arithmetic, the deep probe's baseline and skippedNames are all in the file this
// replaces. They stay covered where they already were: test_dir_cache.cpp on the
// desktop, and sd_selftest.cpp driving the 27-clause contract against a real card.
// This twin is about letting main.cpp LINK, not about testing the card.
//
// AND THE SHARED BUS IS UNMODELLABLE FROM HERE. The card sits on the display's SPI
// bus and SDCardManager does no locking, which is what SpiBusGuard exists for. This
// can assert the guard is HELD; it cannot produce the fault it prevents.
#include "sd_fs.h"

#include <cstring>
#include <string>
#include <vector>

#include "harness_state.h"
#include "reader/host_fs.h"

namespace harness {

// The card's root on the host, and whether it is present at all. A scenario pulls
// the card by clearing `cardPresent()`, which is what pollCardPresence notices.
reader::HostFileSystem& hostFs() {
  static reader::HostFileSystem fs{cardRoot()};
  return fs;
}

// SCRIPTED, NOT EMERGENT. The probe's verdict is what a scenario says it is; the
// real one reads the card and can be wrong in ways only a card can be wrong.
bool& probeSucceeds() {
  static bool ok = true;
  return ok;
}

int& spiGuardDepth() {
  static int d = 0;
  return d;
}

}  // namespace harness

// SpiBusGuard IS A RECORDER HERE, and recording it is the point: the invariant it
// exists for is that no SD traffic crosses a panel refresh, and a transcript that
// shows the guard's extent is what lets a scenario assert the non-overlap even
// though it cannot produce the contention.
SpiBusGuard::SpiBusGuard() {
  if (harness::spiGuardDepth()++ == 0) harness::record("<spi> guard acquired");
}
SpiBusGuard::~SpiBusGuard() {
  if (--harness::spiGuardDepth() == 0) harness::record("<spi> guard released");
}

bool appendToCard(const char* path, const char* data, size_t len, uint32_t capBytes) {
  if (!harness::cardPresent()) return false;
  std::string existing;
  harness::hostFs().readAll(path, existing);
  existing.append(data, len);
  // THE CAP IS A RESERVE, and the real one drops from the FRONT so the newest lines
  // survive -- a log that stopped at the cap would lose exactly the run you wanted.
  if (capBytes > 0 && existing.size() > capBytes)
    existing.erase(0, existing.size() - capBytes);
  return harness::hostFs().writeAll(path, existing);
}

bool SdFileSystem::mount() {
  live_ = harness::cardPresent();
  harness::record("<card> mount live=%d", live_ ? 1 : 0);
  if (!live_) forgetCardFacts();
  return live_;
}

bool SdFileSystem::mounted() const { return live_ && harness::cardPresent(); }

bool SdFileSystem::probe() {
  const bool ok = harness::cardPresent() && harness::probeSucceeds();
  if (!ok) noteCardGone("probe");
  return ok;
}

bool SdFileSystem::useFileProbeTarget(const char* path) {
  if (path == nullptr || *path == '\0') return false;
  probeTarget_ = ProbeTarget::File;
  probeTargetPath_ = path;
  return true;
}

bool SdFileSystem::armDeepProbe() {
  if (!mounted()) return false;
  deepBaseline_ = 1;  // non-zero is what deepProbeArmed() tests
  return true;
}

bool SdFileSystem::deepProbe() {
  const bool ok = harness::cardPresent() && harness::probeSucceeds();
  if (!ok) noteCardGone("deep-probe");
  return ok;
}

void SdFileSystem::forgetCardFacts() {
  listings_.clear();
  dirCounts_.clear();
}

void SdFileSystem::noteCardGone(const char* where) {
  if (!live_) return;
  live_ = false;
  forgetCardFacts();
  // A CARD LOST AFTER A MOUNT CANNOT BE RE-MOUNTED IN PROCESS, which is why the
  // remedy on the device is a restart rather than a retry.
  harness::record("<card> lost at %s", where);
}

bool SdFileSystem::isDirectory(const std::string& p) {
  std::vector<reader::DirEntry> out;
  return harness::hostFs().list(p, out);
}

bool SdFileSystem::isFile(const std::string& p) {
  return harness::hostFs().exists(p) && !isDirectory(p);
}

bool SdFileSystem::readProbeTargetFile() {
  std::string body;
  return harness::hostFs().readAll(probeTargetPath_, body);
}

bool SdFileSystem::readRootDirectory() {
  std::vector<reader::DirEntry> out;
  return harness::hostFs().list("/", out);
}

bool SdFileSystem::exists(std::string_view path) {
  return mounted() && harness::hostFs().exists(path);
}
bool SdFileSystem::list(std::string_view path, std::vector<reader::DirEntry>& out) {
  return mounted() && harness::hostFs().list(path, out);
}
bool SdFileSystem::readAll(std::string_view path, std::string& out) {
  return mounted() && harness::hostFs().readAll(path, out);
}
std::unique_ptr<reader::FileHandle> SdFileSystem::openRead(std::string_view path) {
  return mounted() ? harness::hostFs().openRead(path) : nullptr;
}
bool SdFileSystem::writeAll(std::string_view path, std::string_view data) {
  return mounted() && harness::hostFs().writeAll(path, data);
}
bool SdFileSystem::mkdirs(std::string_view path) {
  return mounted() && harness::hostFs().mkdirs(path);
}
bool SdFileSystem::remove(std::string_view path) {
  if (!mounted()) return false;
  const bool ok = harness::hostFs().remove(path);
  if (ok) ++removals_;
  return ok;
}
