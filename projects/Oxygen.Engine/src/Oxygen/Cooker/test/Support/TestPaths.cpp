//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Cooker/Import/SourceMounts.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>

#if !defined(OXYGEN_COOKER_SOURCE_DIR)                                         \
  || !defined(OXYGEN_COOKER_TEST_ASSETS_DIR)
#  error                                                                       \
    "OXYGEN_COOKER_SOURCE_DIR and OXYGEN_COOKER_TEST_ASSETS_DIR are required"
#endif

namespace oxygen::cooker::test {

namespace {

  auto RequireExisting(std::filesystem::path path) -> std::filesystem::path
  {
    if (!std::filesystem::exists(path)) {
      throw std::runtime_error(
        "Required test input is missing: " + path.string());
    }
    return path;
  }

} // namespace

auto CookerSourceDir() -> const std::filesystem::path&
{
  static const auto dir
    = std::filesystem::path(OXYGEN_COOKER_SOURCE_DIR).lexically_normal();
  return dir;
}

auto AssetsDir() -> const std::filesystem::path&
{
  static const auto dir = RequireExisting(
    std::filesystem::path(OXYGEN_COOKER_TEST_ASSETS_DIR).lexically_normal());
  return dir;
}

auto ModelPath(const std::string_view name) -> std::filesystem::path
{
  return RequireExisting(AssetsDir() / "Models" / std::filesystem::path(name));
}

auto ModelVirtualPath(const std::filesystem::path& source) -> std::string
{
  return "/Cooked/" + source.filename().generic_string();
}

auto SourceMountPath(const std::filesystem::path& source) -> std::string
{
  return "/Content/" + source.filename().generic_string();
}

namespace {

  auto MountTableOf(const std::filesystem::path& root,
    std::vector<content::import::MountDeclaration> declarations)
    -> std::shared_ptr<const content::import::MountTable>
  {
    using content::import::MountDeclarationForm;
    using content::import::MountTable;
    using content::import::MountTableOwner;
    using content::import::MountTableRequest;
    using content::import::ResolveMountTable;
    return std::make_shared<const MountTable>(
      ResolveMountTable(MountTableRequest {
        .project_root = root,
        .declarations = std::move(declarations),
        .form = MountDeclarationForm::kCommittedFile,
        .declared_in = "test",
        .working_directory = root,
        .mounts_option = std::nullopt,
        .owner = MountTableOwner::kProject,
      }));
  }

} // namespace

auto MountPathIn(const std::filesystem::path& root,
  const std::filesystem::path& file) -> std::string
{
  return "/Content/" + file.lexically_relative(root).generic_string();
}

auto ContentMountTable(const std::filesystem::path& root)
  -> std::shared_ptr<const content::import::MountTable>
{
  return SourceMountTable("Content", root);
}

auto SourceMountTable(
  const std::string_view name, const std::filesystem::path& root)
  -> std::shared_ptr<const content::import::MountTable>
{
  return MountTableOf(root,
    {
      content::import::MountDeclaration {
        .name = std::string { name },
        .kind = content::import::MountKind::kSource,
        .path = ".",
      },
    });
}

auto ContentAndOtherMountTable(const std::filesystem::path& root)
  -> std::shared_ptr<const content::import::MountTable>
{
  using content::import::MountDeclaration;
  using content::import::MountKind;
  return MountTableOf(root,
    {
      MountDeclaration {
        .name = "Content",
        .kind = MountKind::kSource,
        .path = "Content",
      },
      MountDeclaration {
        .name = "Other",
        .kind = MountKind::kSource,
        .path = "Other",
      },
    });
}

auto SchemaPath(const std::string_view relative_to_cooker)
  -> std::filesystem::path
{
  return RequireExisting(
    CookerSourceDir() / std::filesystem::path(relative_to_cooker));
}

} // namespace oxygen::cooker::test
