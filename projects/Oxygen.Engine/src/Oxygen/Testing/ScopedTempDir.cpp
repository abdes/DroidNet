//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

#include <fmt/format.h>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Platforms.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/ScopedTempDir.h>

#ifdef OXYGEN_WINDOWS
#  include <Windows.h> // IWYU pragma: keep

#  include <processthreadsapi.h>
#else
#  include <unistd.h>
#endif

namespace oxygen::testing {

namespace {

  auto CurrentProcessId() -> uint64_t
  {
#ifdef OXYGEN_WINDOWS
    return static_cast<uint64_t>(::GetCurrentProcessId());
#else
    return static_cast<uint64_t>(::getpid());
#endif
  }

  auto TestsRoot() -> std::filesystem::path
  {
    return std::filesystem::temp_directory_path() / "oxygen-tests";
  }

  //! The running test's `suite.name`, safe as one path component.
  auto CurrentTestName() -> std::string
  {
    const auto* info = ::testing::UnitTest::GetInstance()->current_test_info();
    if (info == nullptr) {
      return "no-test";
    }
    auto name = std::string(info->test_suite_name()) + "." + info->name();
    // Typed and parameterized names contain '/', which must not nest.
    std::ranges::replace_if(
      name, [](const char c) { return c == '/' || c == '\\' || c == ':'; },
      '_');
    return name;
  }

  auto IsUnder(const std::filesystem::path& path,
    const std::filesystem::path& base) -> bool
  {
    const auto [base_end, path_it] = std::ranges::mismatch(base, path);
    return base_end == base.end() && path_it != path.end();
  }

} // namespace

ScopedTempDir::ScopedTempDir()
{
  static auto counter = std::atomic_uint64_t { 0 };
  path_ = TestsRoot()
    / fmt::format(
      "pid-{}-{}-{}", CurrentProcessId(), CurrentTestName(), ++counter);
  std::filesystem::create_directories(base::ToNativePath(path_));
}

ScopedTempDir::~ScopedTempDir()
{
  auto error = std::error_code {};
  const auto root = std::filesystem::weakly_canonical(TestsRoot(), error);
  if (error) {
    return;
  }
  const auto target = std::filesystem::weakly_canonical(path_, error);
  if (error || !IsUnder(target, root)) {
    return;
  }
  std::filesystem::remove_all(base::ToNativePath(target), error);
}

auto ScopedTempDir::File(const std::string_view relative) const
  -> std::filesystem::path
{
  auto path = path_ / std::filesystem::path(relative);
  if (const auto parent = path.parent_path(); !parent.empty()) {
    std::filesystem::create_directories(base::ToNativePath(parent));
  }
  return path;
}

} // namespace oxygen::testing
