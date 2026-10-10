//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <atomic>
#include <cstdint>
#include <sstream>
#include <string>
#include <system_error>

#if defined(_WIN32)
#  include <Windows.h>
#else
#  include <unistd.h>
#endif

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>

namespace oxygen::cooker::test {

namespace {

  auto CurrentProcessId() -> uint64_t
  {
#if defined(_WIN32)
    return static_cast<uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
  }

  auto TestsRoot() -> std::filesystem::path
  {
    return std::filesystem::temp_directory_path() / "oxygen-cooker-tests";
  }

  auto CurrentTestName() -> std::string
  {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (info == nullptr) {
      return "no-test";
    }
    auto name = std::string(info->test_suite_name()) + "." + info->name();
    // Parameterized names contain '/', which must not create subdirectories.
    for (auto& c : name) {
      if (c == '/' || c == '\\' || c == ':') {
        c = '_';
      }
    }
    return name;
  }

  auto IsUnder(const std::filesystem::path& path,
    const std::filesystem::path& base) -> bool
  {
    auto path_it = path.begin();
    for (auto base_it = base.begin(); base_it != base.end();
      ++base_it, ++path_it) {
      if (path_it == path.end() || *path_it != *base_it) {
        return false;
      }
    }
    return path_it != path.end();
  }

} // namespace

ScopedTempDir::ScopedTempDir()
{
  static auto counter = std::atomic_uint64_t { 0U };
  auto leaf = std::ostringstream {};
  leaf << "pid-" << CurrentProcessId() << "-" << CurrentTestName() << "-"
       << ++counter;
  path_ = TestsRoot() / leaf.str();
  std::filesystem::create_directories(base::ToNativePath(path_));
}

ScopedTempDir::~ScopedTempDir()
{
  auto ec = std::error_code {};
  const auto root = std::filesystem::weakly_canonical(TestsRoot(), ec);
  if (ec) {
    return;
  }
  const auto target = std::filesystem::weakly_canonical(path_, ec);
  if (ec || !IsUnder(target, root)) {
    return;
  }
  std::filesystem::remove_all(base::ToNativePath(target), ec);
}

auto TempDirTest::TempPath(const std::string_view relative) const
  -> std::filesystem::path
{
  auto path = TempDir() / std::filesystem::path(relative);
  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(base::ToNativePath(parent));
  }
  return path;
}

} // namespace oxygen::cooker::test
