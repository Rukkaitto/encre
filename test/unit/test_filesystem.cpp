// The FileSystem contract, written ONCE and run against every implementation.
//
// The cases below take a `FileSystem&`, so the in-memory fake and the real
// HostFileSystem are held to exactly the same promises. That is the point of the
// file: a fake that passes tests the real one fails is worse than no fake,
// because every test written above it then proves nothing.
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "doctest.h"
#include "fake_fs.h"
#include "reader/filesystem.h"
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

// list()'s order is unspecified, so every assertion about a listing goes
// through here.
std::vector<DirEntry> sortedByName(std::vector<DirEntry> v) {
  std::sort(v.begin(), v.end(),
            [](const DirEntry& a, const DirEntry& b) { return a.name < b.name; });
  return v;
}

const DirEntry* entryNamed(const std::vector<DirEntry>& v, const std::string& name) {
  for (const auto& e : v)
    if (e.name == name) return &e;
  return nullptr;
}

// `fs` must be mounted and empty. Every implementation owes all of this.
void checkFileSystemContract(FileSystem& fs, const char* label) {
  INFO("implementation: " << label);
  REQUIRE(fs.mounted());

  SUBCASE("a written file exists and reads back byte-identical") {
    REQUIRE(fs.writeAll("/hello.txt", "hello"));
    CHECK(fs.exists("/hello.txt"));
    std::string got;
    REQUIRE(fs.readAll("/hello.txt", got));
    CHECK(got == "hello");
  }

  SUBCASE("embedded newlines and NULs survive the round trip") {
    const std::string body("li\nne\0with\r\nnul", 15);
    REQUIRE(fs.writeAll("/binary.bin", body));
    std::string got;
    REQUIRE(fs.readAll("/binary.bin", got));
    CHECK(got.size() == body.size());
    CHECK(got == body);
  }

  SUBCASE("an empty file is a file, not an absence") {
    REQUIRE(fs.writeAll("/empty.txt", ""));
    CHECK(fs.exists("/empty.txt"));
    std::string got = "sentinel";
    REQUIRE(fs.readAll("/empty.txt", got));
    CHECK(got.empty());
    std::vector<DirEntry> out;
    REQUIRE(fs.list("/", out));
    const DirEntry* e = entryNamed(out, "empty.txt");
    REQUIRE(e != nullptr);
    CHECK(e->size == 0u);
    CHECK_FALSE(e->isDir);
  }

  SUBCASE("writeAll truncates rather than appending") {
    REQUIRE(fs.writeAll("/t.txt", "a long original body"));
    REQUIRE(fs.writeAll("/t.txt", "short"));
    std::string got;
    REQUIRE(fs.readAll("/t.txt", got));
    CHECK(got == "short");
  }

  SUBCASE("writeAll creates missing parents") {
    CHECK_FALSE(fs.exists("/.reader"));
    REQUIRE(fs.writeAll("/.reader/deep/settings.json", "{}"));
    CHECK(fs.exists("/.reader"));
    CHECK(fs.exists("/.reader/deep"));
    CHECK(fs.exists("/.reader/deep/settings.json"));
    std::string got;
    REQUIRE(fs.readAll("/.reader/deep/settings.json", got));
    CHECK(got == "{}");
  }

  SUBCASE("list appends rather than clearing, and reports isDir and size") {
    REQUIRE(fs.mkdirs("/books/sub"));
    REQUIRE(fs.writeAll("/books/a.txt", "12345"));

    std::vector<DirEntry> out;
    out.push_back(DirEntry{"pre-existing", false, 99});
    REQUIRE(fs.list("/books", out));
    REQUIRE(out.size() == 3);
    CHECK(out[0].name == "pre-existing");  // appended, not cleared

    const auto s = sortedByName(std::vector<DirEntry>(out.begin() + 1, out.end()));
    CHECK(s[0].name == "a.txt");
    CHECK_FALSE(s[0].isDir);
    CHECK(s[0].size == 5u);
    CHECK(s[1].name == "sub");
    CHECK(s[1].isDir);
    CHECK(s[1].size == 0u);  // 0 for directories
  }

  SUBCASE("list names leaves, not paths") {
    REQUIRE(fs.writeAll("/dir/leaf.txt", "x"));
    std::vector<DirEntry> out;
    REQUIRE(fs.list("/dir", out));
    REQUIRE(out.size() == 1);
    CHECK(out[0].name == "leaf.txt");
  }

  SUBCASE("list on a file, or on a missing path, is false and leaves out alone") {
    REQUIRE(fs.writeAll("/file.txt", "x"));
    std::vector<DirEntry> out;
    out.push_back(DirEntry{"pre-existing", false, 99});
    CHECK_FALSE(fs.list("/file.txt", out));
    CHECK(out.size() == 1);
    CHECK_FALSE(fs.list("/nope", out));
    CHECK(out.size() == 1);
    CHECK_FALSE(fs.list("/nope/deeper", out));
    CHECK(out.size() == 1);
  }

  SUBCASE("the root is a listable directory") {
    REQUIRE(fs.writeAll("/top.txt", "x"));
    std::vector<DirEntry> out;
    REQUIRE(fs.list("/", out));
    CHECK(entryNamed(out, "top.txt") != nullptr);
  }

  SUBCASE("readAll on a missing file is false and leaves out untouched") {
    std::string got = "sentinel";
    CHECK_FALSE(fs.readAll("/absent.txt", got));
    CHECK(got == "sentinel");
    // ...including when the parent directory does not exist either.
    CHECK_FALSE(fs.readAll("/no/such/dir/absent.txt", got));
    CHECK(got == "sentinel");
  }

  SUBCASE("readAll on a directory is false") {
    REQUIRE(fs.mkdirs("/adir"));
    std::string got = "sentinel";
    CHECK_FALSE(fs.readAll("/adir", got));
    CHECK(got == "sentinel");
  }

  SUBCASE("remove is about the end state") {
    CHECK(fs.remove("/never-existed.txt"));  // already gone counts as removed
    REQUIRE(fs.writeAll("/doomed.txt", "x"));
    CHECK(fs.remove("/doomed.txt"));
    CHECK_FALSE(fs.exists("/doomed.txt"));
    CHECK(fs.remove("/doomed.txt"));  // and again, idempotently
  }

  SUBCASE("remove refuses a directory") {
    REQUIRE(fs.mkdirs("/keepme"));
    CHECK_FALSE(fs.remove("/keepme"));
    CHECK(fs.exists("/keepme"));
  }

  SUBCASE("mkdirs is idempotent and creates the whole chain") {
    REQUIRE(fs.mkdirs("/a/b/c"));
    CHECK(fs.exists("/a"));
    CHECK(fs.exists("/a/b"));
    CHECK(fs.exists("/a/b/c"));
    CHECK(fs.mkdirs("/a/b/c"));  // already there
    CHECK(fs.mkdirs("/"));       // the root always exists
  }

  SUBCASE("a file cannot be a parent directory") {
    REQUIRE(fs.writeAll("/blocker", "x"));
    CHECK_FALSE(fs.mkdirs("/blocker/under"));
    CHECK_FALSE(fs.writeAll("/blocker/under/f.txt", "x"));
    CHECK_FALSE(fs.exists("/blocker/under"));
    std::string got;
    REQUIRE(fs.readAll("/blocker", got));
    CHECK(got == "x");  // and the file itself is untouched
  }

  SUBCASE("writeAll refuses a path that is a directory") {
    REQUIRE(fs.mkdirs("/adir"));
    CHECK_FALSE(fs.writeAll("/adir", "x"));
    CHECK(fs.exists("/adir"));
    std::vector<DirEntry> out;
    CHECK(fs.list("/adir", out));  // still a directory
  }

  SUBCASE("a redundant or trailing separator addresses the same thing") {
    REQUIRE(fs.writeAll("/n/f.txt", "x"));
    CHECK(fs.exists("/n//f.txt"));
    CHECK(fs.exists("/n/"));
    CHECK(fs.exists("/n/f.txt/"));  // stripped, not treated as a directory
    std::string got;
    REQUIRE(fs.readAll("//n//f.txt", got));
    CHECK(got == "x");
    std::vector<DirEntry> out;
    CHECK(fs.list("/n/", out));
  }
}

// `fs` must report mounted() == false. Nothing may succeed, and nothing may
// bring the storage into existence as a side effect.
void checkUnmountedContract(FileSystem& fs, const char* label) {
  INFO("implementation: " << label);
  REQUIRE_FALSE(fs.mounted());

  CHECK_FALSE(fs.exists("/anything"));
  CHECK_FALSE(fs.exists("/"));

  std::vector<DirEntry> out;
  out.push_back(DirEntry{"pre-existing", false, 99});
  CHECK_FALSE(fs.list("/", out));
  CHECK(out.size() == 1);

  std::string got = "sentinel";
  CHECK_FALSE(fs.readAll("/anything", got));
  CHECK(got == "sentinel");

  CHECK_FALSE(fs.writeAll("/anything", "x"));
  CHECK_FALSE(fs.mkdirs("/anything"));
  // remove's end-state contract does NOT apply here: with no storage we cannot
  // know the file is gone, so claiming success would be a lie.
  CHECK_FALSE(fs.remove("/anything"));
}

}  // namespace

TEST_CASE("FakeFileSystem obeys the FileSystem contract") {
  FakeFileSystem fs;
  checkFileSystemContract(fs, "FakeFileSystem");
}

TEST_CASE("FakeFileSystem obeys the unmounted contract") {
  FakeFileSystem fs;
  fs.setMounted(false);
  checkUnmountedContract(fs, "FakeFileSystem");
}

// The same cases, against real files. If these two diverge the fake is lying,
// and everything tested against the fake is worthless.

TEST_CASE("HostFileSystem obeys the FileSystem contract") {
  const std::string root = freshTempRoot("contract");
  HostFileSystem fs(root);
  checkFileSystemContract(fs, "HostFileSystem");
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
}

TEST_CASE("HostFileSystem obeys the unmounted contract") {
  const std::string root = std::string(BUILD_DIR) + "/fs_test/no_such_root";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  HostFileSystem fs(root);
  checkUnmountedContract(fs, "HostFileSystem");
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
