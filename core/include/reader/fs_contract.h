#pragma once
// The FileSystem contract, written once and driven by more than one runner.
//
// Why this is a header in core/ rather than a block of doctest cases: `shell/`
// has no test harness, so SdFileSystem -- the ONE implementation that talks to
// real hardware, on the one bus that can go wrong -- is the one nothing would
// hold to the promises in filesystem.h. The clauses below report through
// FsContractReport instead of a test macro, so the same assertions run two ways:
//
//   * test/unit/test_filesystem.cpp adapts the report to doctest and drives it
//     against FakeFileSystem and HostFileSystem, on every `make test`.
//   * shell/src/sd_selftest.cpp adapts it to Serial and drives it against a real
//     card on the device, built in only with -DENCRE_FS_SELFTEST=1.
//
// core/ is the only place both of those can include from, which is the whole
// reason a self-test lives here. It is header-only and includes nothing beyond
// the standard library and filesystem.h, so a firmware build that does not
// include it pays nothing for it.
//
// EVERY CLAUSE NEEDS A MOUNTED, EMPTY FILESYSTEM, and clauses are independent:
// the runner hands each one fresh storage. doctest re-enters its SUBCASE with a
// new instance; the device runner wipes its scratch directory between clauses.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "reader/filesystem.h"

namespace reader {

// Where a clause's assertions go. A runner implements this; the clauses know
// nothing about doctest or about Serial.
class FsContractReport {
 public:
  virtual ~FsContractReport() = default;

  // One assertion whose failure still leaves the rest of the clause meaningful.
  virtual void check(bool passed, const char* expr) = 0;

  // One the clause cannot continue without -- the equivalent of doctest's
  // REQUIRE. Returns `passed`; the clause returns immediately when it is false,
  // so a runner whose report cannot unwind (no exceptions on the device) still
  // stops at the same point the desktop one does.
  virtual bool require(bool passed, const char* expr) = 0;
};

// One independent case. `name` is what a runner labels it with -- it is also a
// doctest SUBCASE name, so keep it filesystem-safe-ish and human.
struct FsContractClause {
  const char* name;
  void (*run)(FileSystem& fs, FsContractReport& r);
};

// list()'s order is unspecified, so every assertion about a listing goes through
// here.
inline std::vector<DirEntry> fsSortedByName(std::vector<DirEntry> v) {
  std::sort(v.begin(), v.end(),
            [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return v;
}

inline const DirEntry* fsEntryNamed(const std::vector<DirEntry>& v, const std::string& name) {
  for (const auto& e : v)
    if (e.name == name) return &e;
  return nullptr;
}

// The two assertion forms, spelled so that the reported text is the source text.
// Both are #undef'd at the end of the header: nothing outside it should see them.
#define FSC_CHECK(cond) r.check(static_cast<bool>(cond), #cond)
#define FSC_REQUIRE(cond)                                   \
  do {                                                      \
    if (!r.require(static_cast<bool>(cond), #cond)) return;  \
  } while (false)

namespace fs_contract {

inline void roundTrip(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/hello.txt", "hello"));
  FSC_CHECK(fs.exists("/hello.txt"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/hello.txt", got));
  FSC_CHECK(got == "hello");
}

inline void binarySafe(FileSystem& fs, FsContractReport& r) {
  const std::string body("li\nne\0with\r\nnul", 15);
  FSC_REQUIRE(fs.writeAll("/binary.bin", body));
  std::string got;
  FSC_REQUIRE(fs.readAll("/binary.bin", got));
  FSC_CHECK(got.size() == body.size());
  FSC_CHECK(got == body);
}

inline void emptyFileIsAFile(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/empty.txt", ""));
  FSC_CHECK(fs.exists("/empty.txt"));
  std::string got = "sentinel";
  FSC_REQUIRE(fs.readAll("/empty.txt", got));
  FSC_CHECK(got.empty());
  std::vector<DirEntry> out;
  FSC_REQUIRE(fs.list("/", out));
  const DirEntry* e = fsEntryNamed(out, "empty.txt");
  FSC_REQUIRE(e != nullptr);
  FSC_CHECK(e->size == 0u);
  FSC_CHECK(!e->isDir);
}

inline void writeTruncates(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/t.txt", "a long original body"));
  FSC_REQUIRE(fs.writeAll("/t.txt", "short"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/t.txt", got));
  FSC_CHECK(got == "short");
}

inline void writeCreatesParents(FileSystem& fs, FsContractReport& r) {
  FSC_CHECK(!fs.exists("/.reader"));
  FSC_REQUIRE(fs.writeAll("/.reader/deep/settings.json", "{}"));
  FSC_CHECK(fs.exists("/.reader"));
  FSC_CHECK(fs.exists("/.reader/deep"));
  FSC_CHECK(fs.exists("/.reader/deep/settings.json"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/.reader/deep/settings.json", got));
  FSC_CHECK(got == "{}");
}

inline void listAppendsAndReports(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/books/sub"));
  FSC_REQUIRE(fs.writeAll("/books/a.txt", "12345"));

  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  FSC_REQUIRE(fs.list("/books", out));
  FSC_REQUIRE(out.size() == 3);
  FSC_CHECK(out[0].name == "pre-existing");  // appended, not cleared

  const auto s = fsSortedByName(std::vector<DirEntry>(out.begin() + 1, out.end()));
  FSC_CHECK(s[0].name == "a.txt");
  FSC_CHECK(!s[0].isDir);
  FSC_CHECK(s[0].size == 5u);
  FSC_CHECK(s[1].name == "sub");
  FSC_CHECK(s[1].isDir);
  FSC_CHECK(s[1].size == 0u);  // 0 for directories
}

inline void listNamesLeaves(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/dir/leaf.txt", "x"));
  std::vector<DirEntry> out;
  FSC_REQUIRE(fs.list("/dir", out));
  FSC_REQUIRE(out.size() == 1);
  FSC_CHECK(out[0].name == "leaf.txt");
}

inline void listRefusesFileAndMissing(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/file.txt", "x"));
  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  FSC_CHECK(!fs.list("/file.txt", out));
  FSC_CHECK(out.size() == 1);
  FSC_CHECK(!fs.list("/nope", out));
  FSC_CHECK(out.size() == 1);
  FSC_CHECK(!fs.list("/nope/deeper", out));
  FSC_CHECK(out.size() == 1);
}

inline void rootIsListable(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/top.txt", "x"));
  std::vector<DirEntry> out;
  FSC_REQUIRE(fs.list("/", out));
  FSC_CHECK(fsEntryNamed(out, "top.txt") != nullptr);
}

inline void readAllMissingLeavesOut(FileSystem& fs, FsContractReport& r) {
  std::string got = "sentinel";
  FSC_CHECK(!fs.readAll("/absent.txt", got));
  FSC_CHECK(got == "sentinel");
  // ...including when the parent directory does not exist either.
  FSC_CHECK(!fs.readAll("/no/such/dir/absent.txt", got));
  FSC_CHECK(got == "sentinel");
}

inline void readAllRefusesDirectory(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/adir"));
  std::string got = "sentinel";
  FSC_CHECK(!fs.readAll("/adir", got));
  FSC_CHECK(got == "sentinel");
}

inline void removeIsAboutTheEndState(FileSystem& fs, FsContractReport& r) {
  FSC_CHECK(fs.remove("/never-existed.txt"));  // already gone counts as removed
  FSC_REQUIRE(fs.writeAll("/doomed.txt", "x"));
  FSC_CHECK(fs.remove("/doomed.txt"));
  FSC_CHECK(!fs.exists("/doomed.txt"));
  FSC_CHECK(fs.remove("/doomed.txt"));  // and again, idempotently
}

inline void removeRefusesDirectory(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/keepme"));
  FSC_CHECK(!fs.remove("/keepme"));
  FSC_CHECK(fs.exists("/keepme"));
}

inline void mkdirsIsIdempotent(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/a/b/c"));
  FSC_CHECK(fs.exists("/a"));
  FSC_CHECK(fs.exists("/a/b"));
  FSC_CHECK(fs.exists("/a/b/c"));
  FSC_CHECK(fs.mkdirs("/a/b/c"));  // already there
  FSC_CHECK(fs.mkdirs("/"));       // the root always exists
}

inline void fileCannotBeAParent(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/blocker", "x"));
  FSC_CHECK(!fs.mkdirs("/blocker/under"));
  FSC_CHECK(!fs.writeAll("/blocker/under/f.txt", "x"));
  FSC_CHECK(!fs.exists("/blocker/under"));
  std::string got;
  FSC_REQUIRE(fs.readAll("/blocker", got));
  FSC_CHECK(got == "x");  // and the file itself is untouched
}

inline void writeRefusesDirectory(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.mkdirs("/adir"));
  FSC_CHECK(!fs.writeAll("/adir", "x"));
  FSC_CHECK(fs.exists("/adir"));
  std::vector<DirEntry> out;
  FSC_CHECK(fs.list("/adir", out));  // still a directory
}

inline void separatorsNormalise(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(fs.writeAll("/n/f.txt", "x"));
  FSC_CHECK(fs.exists("/n//f.txt"));
  FSC_CHECK(fs.exists("/n/"));
  FSC_CHECK(fs.exists("/n/f.txt/"));  // stripped, not treated as a directory
  std::string got;
  FSC_REQUIRE(fs.readAll("//n//f.txt", got));
  FSC_CHECK(got == "x");
  std::vector<DirEntry> out;
  FSC_CHECK(fs.list("/n/", out));
}

// Nothing may succeed, and nothing may bring the storage into existence as a
// side effect. Runs against a filesystem whose mounted() is false, which is the
// one clause that does NOT want empty-and-mounted storage.
inline void unmounted(FileSystem& fs, FsContractReport& r) {
  FSC_REQUIRE(!fs.mounted());

  FSC_CHECK(!fs.exists("/anything"));
  FSC_CHECK(!fs.exists("/"));

  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  FSC_CHECK(!fs.list("/", out));
  FSC_CHECK(out.size() == 1);

  std::string got = "sentinel";
  FSC_CHECK(!fs.readAll("/anything", got));
  FSC_CHECK(got == "sentinel");

  FSC_CHECK(!fs.writeAll("/anything", "x"));
  FSC_CHECK(!fs.mkdirs("/anything"));
  // remove's end-state contract does NOT apply here: with no storage we cannot
  // know the file is gone, so claiming success would be a lie.
  FSC_CHECK(!fs.remove("/anything"));
}

}  // namespace fs_contract

#undef FSC_CHECK
#undef FSC_REQUIRE

// The clauses that need a mounted, empty filesystem, in the order they were
// written. `count` is set to the number returned.
inline const FsContractClause* fsContractClauses(size_t& count) {
  static const FsContractClause kClauses[] = {
      {"a written file exists and reads back byte-identical", &fs_contract::roundTrip},
      {"embedded newlines and NULs survive the round trip", &fs_contract::binarySafe},
      {"an empty file is a file, not an absence", &fs_contract::emptyFileIsAFile},
      {"writeAll truncates rather than appending", &fs_contract::writeTruncates},
      {"writeAll creates missing parents", &fs_contract::writeCreatesParents},
      {"list appends rather than clearing, and reports isDir and size",
       &fs_contract::listAppendsAndReports},
      {"list names leaves, not paths", &fs_contract::listNamesLeaves},
      {"list on a file, or on a missing path, is false and leaves out alone",
       &fs_contract::listRefusesFileAndMissing},
      {"the root is a listable directory", &fs_contract::rootIsListable},
      {"readAll on a missing file is false and leaves out untouched",
       &fs_contract::readAllMissingLeavesOut},
      {"readAll on a directory is false", &fs_contract::readAllRefusesDirectory},
      {"remove is about the end state", &fs_contract::removeIsAboutTheEndState},
      {"remove refuses a directory", &fs_contract::removeRefusesDirectory},
      {"mkdirs is idempotent and creates the whole chain", &fs_contract::mkdirsIsIdempotent},
      {"a file cannot be a parent directory", &fs_contract::fileCannotBeAParent},
      {"writeAll refuses a path that is a directory", &fs_contract::writeRefusesDirectory},
      {"a redundant or trailing separator addresses the same thing",
       &fs_contract::separatorsNormalise},
  };
  count = sizeof(kClauses) / sizeof(kClauses[0]);
  return kClauses;
}

// The single clause for a filesystem with no storage behind it. Run it with
// `clause.run(fs, r)` directly, NOT through fsRunClause below -- its whole point
// is a filesystem whose mounted() is false, which is the one thing fsRunClause
// refuses.
inline const FsContractClause& fsUnmountedClause() {
  static const FsContractClause kClause{"with mounted() false, nothing succeeds",
                                        &fs_contract::unmounted};
  return kClause;
}

// Runs one clause. The mounted() precondition is asserted here rather than in
// each clause so that both runners agree on what "handed fresh storage" means,
// and so a runner that forgot to create its scratch directory fails loudly
// instead of reporting seventeen unrelated failures.
inline void fsRunClause(const FsContractClause& clause, FileSystem& fs, FsContractReport& r) {
  if (!r.require(fs.mounted(), "fs.mounted()")) return;
  clause.run(fs, r);
}

}  // namespace reader
