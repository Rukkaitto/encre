// The FileSystem contract, written ONCE and run against every implementation.
//
// The clauses themselves live in reader/fs_contract.h and take a `FileSystem&`,
// so the in-memory fake and the real HostFileSystem are held to exactly the same
// promises. That is the point: a fake that passes tests the real one fails is
// worse than no fake, because every test written above it then proves nothing.
//
// They live in core/ rather than here because `shell/` has no test harness, and
// SdFileSystem -- the implementation on real hardware, on the shared SPI bus --
// would otherwise be the one nothing checks. This file is the doctest runner for
// those clauses; shell/src/sd_selftest.cpp is the on-device one, and they run the
// same assertions in the same order.
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/filesystem.h"
#include "reader/fs_contract.h"
#include "reader/host_fs.h"

using namespace reader;

namespace {

// A directory under BUILD_DIR, emptied first so the test does not inherit
// anything from a previous run. doctest re-enters a TEST_CASE body once per
// SUBCASE, so this runs before each contract case and every one of them starts
// from an empty filesystem -- the same state a freshly constructed fake is in.
std::string freshTempRoot(const char* name) {
  const std::string root = std::string(BUILD_DIR) + "/fs_test/" + name;
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  REQUIRE_MESSAGE(!ec, "cannot create " << root << ": " << ec.message());
  return root;
}

// Adapts the contract's report to doctest, one assertion each so the counts and
// the failure behaviour are exactly what the hand-written CHECK/REQUIRE calls
// were before the clauses moved into reader/fs_contract.h.
//
// `expr` IS WRAPPED IN std::string, and that is not a style choice. doctest
// resolves a bare `const char*` to its `const void*` stringification overload and
// prints the POINTER, so every failure here used to read
//   ERROR: CHECK( passed ) is NOT correct!  logged: 0x10088d367
// -- a hex address where the clause's own source text should be. fs_contract.h
// spells FSC_CHECK as `r.check(cond, #cond)` precisely so that a failure quotes
// the condition that failed; without this cast that whole mechanism was silently
// producing addresses on the desktop, while the device runner (a Serial.printf
// with %s) printed the text correctly all along. So the ONE runner that runs on
// every `make test` was the one that could not say what broke.
class DoctestReport : public FsContractReport {
 public:
  void check(bool passed, const char* expr) override { CHECK_MESSAGE(passed, std::string(expr)); }
  bool require(bool passed, const char* expr) override {
    REQUIRE_MESSAGE(passed, std::string(expr));  // throws past the clause on failure
    return passed;
  }
};

// Every mounted clause, each against storage the factory has just made fresh.
// The SUBCASE is what gives each clause its own filesystem: doctest re-enters the
// body once per clause, so `make` runs twenty-seven independent cases and a failure
// names which one.
template <typename Factory>
void runContract(const char* label, Factory makeFs) {
  // std::string, not the bare const char*: doctest resolves a const char* to its
  // const void* overload and prints the POINTER, so this message used to read
  // "implementation: 0x1029c54ab" -- on the one runner whose whole job is to say
  // WHICH implementation broke the contract. test_json.cpp wraps it for the same
  // reason.
  INFO("implementation: " << std::string(label));
  size_t count = 0;
  const FsContractClause* clauses = fsContractClauses(count);
  for (size_t i = 0; i < count; ++i) {
    SUBCASE(clauses[i].name) {
      auto fs = makeFs();
      DoctestReport report;
      fsRunClause(clauses[i], *fs, report);
    }
  }
}

}  // namespace

TEST_CASE("FakeFileSystem obeys the FileSystem contract") {
  runContract("FakeFileSystem", [] { return std::make_unique<FakeFileSystem>(); });
}

TEST_CASE("FakeFileSystem obeys the unmounted contract") {
  INFO("implementation: FakeFileSystem");
  FakeFileSystem fs;
  fs.setMounted(false);
  DoctestReport report;
  fsUnmountedClause().run(fs, report);
}

// The same cases, against real files. If these two diverge the fake is lying,
// and everything tested against the fake is worthless.

TEST_CASE("HostFileSystem obeys the FileSystem contract") {
  runContract("HostFileSystem",
              [] { return std::make_unique<HostFileSystem>(freshTempRoot("contract")); });
  std::error_code ec;
  std::filesystem::remove_all(std::string(BUILD_DIR) + "/fs_test/contract", ec);
}

TEST_CASE("HostFileSystem obeys the unmounted contract") {
  INFO("implementation: HostFileSystem");
  const std::string root = std::string(BUILD_DIR) + "/fs_test/no_such_root";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  HostFileSystem fs(root);
  DoctestReport report;
  fsUnmountedClause().run(fs, report);
  // A missing card must stay missing: not one of those calls may have created
  // the root on its way to failing.
  CHECK_FALSE(std::filesystem::exists(root));
}

// HostFileSystem's own concern: the rooting.

TEST_CASE("HostFileSystem confines an absolute reader path to its root") {
  const std::string root = freshTempRoot("rooting");
  HostFileSystem fs(root);

  REQUIRE(fs.writeAll("/.reader/settings.json", "{\"version\":1}"));
  CHECK(std::filesystem::exists(root + "/.reader/settings.json"));
  CHECK(fs.hostPath("/.reader/settings.json") == root + "/.reader/settings.json");

  // A file the host put there is visible through the interface.
  {
    std::ofstream out(root + "/planted.txt", std::ios::binary);
    out << "planted";
  }
  std::string got;
  REQUIRE(fs.readAll("/planted.txt", got));
  CHECK(got == "planted");

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

// HostFileSystem's other own concern: a handle reports the length it read at
// open, and the host filesystem is the one place something else can change the
// file underneath it -- the simulator writes into the same tree it reads from.
// This cannot be a contract clause: the fake's handle holds a copy, and asking
// the device runner to grow a file mid-clause would be testing SdFat rather than
// us. So it is pinned here, against the implementation that has the hazard.
TEST_CASE("a host handle does not read past the size it reported, if the file grows") {
  const std::string root = freshTempRoot("grow");
  HostFileSystem fs(root);
  REQUIRE(fs.writeAll("/f.bin", "12345"));

  std::unique_ptr<FileHandle> h = fs.openRead("/f.bin");
  REQUIRE(h != nullptr);
  REQUIRE(h->size() == 5u);

  // Append behind the handle's back.
  {
    std::ofstream out(root + "/f.bin", std::ios::binary | std::ios::app);
    out << "6789";
  }

  char buf[16] = {0};
  CHECK(h->read(buf, sizeof(buf)) == 5u);  // the length it promised, not what is there now
  CHECK(std::string(buf, 5) == "12345");
  CHECK(h->position() == 5u);
  CHECK(h->position() <= h->size());  // the invariant the clamp exists for
  CHECK(h->read(buf, sizeof(buf)) == 0u);
  // ...and seek is still bounded by the size it reported, not the new one.
  CHECK_FALSE(h->seek(7));
  CHECK(h->position() == 5u);

  // A handle opened NOW sees the longer file: size() is per-handle, read at open.
  std::unique_ptr<FileHandle> fresh = fs.openRead("/f.bin");
  REQUIRE(fresh != nullptr);
  CHECK(fresh->size() == 9u);

  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

// The fake's own extras -- the injectable failures the product tests need, which
// no real implementation can be asked to reproduce on demand.

TEST_CASE("the fake can be unmounted and mounted again with its contents intact") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/f.txt", "x"));
  fs.setMounted(false);
  CHECK_FALSE(fs.exists("/f.txt"));
  fs.setMounted(true);
  CHECK(fs.exists("/f.txt"));
}

TEST_CASE("the fake can refuse every write, so save-failure handling is testable") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/f.txt", "before"));
  fs.setFailWrites(true);
  CHECK_FALSE(fs.writeAll("/f.txt", "after"));
  CHECK_FALSE(fs.writeAll("/other.txt", "new"));
  CHECK_FALSE(fs.exists("/other.txt"));
  std::string got;
  REQUIRE(fs.readAll("/f.txt", got));
  CHECK(got == "before");  // a refused write did not damage what was there
  fs.setFailWrites(false);
  CHECK(fs.writeAll("/f.txt", "after"));
}

TEST_CASE("the fake exposes its contents so a test can assert on the file written") {
  FakeFileSystem fs;
  REQUIRE(fs.writeAll("/.reader/settings.json", "{}"));
  CHECK(fs.fileCount() == 1);
  const std::string* body = fs.peek("/.reader/settings.json");
  REQUIRE(body != nullptr);
  CHECK(*body == "{}");
  CHECK(fs.peek("/nope") == nullptr);
}
