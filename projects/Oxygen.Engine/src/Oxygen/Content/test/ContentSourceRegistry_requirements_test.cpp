//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Fixtures/FakeContentSource.h"

#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Content/DependencyRequirementError.h>
#include <Oxygen/Content/Internal/ContentSourceRegistry.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/PakCatalog.h>
#include <Oxygen/Data/SourceKey.h>
#include <Oxygen/Testing/GTest.h>

namespace oxygen::content::testing {
namespace {

  using data::AssetKey;
  using data::AssetType;
  using data::KeyReferenceKind;
  using data::PakRequirement;
  using internal::ContentSourceRegistry;

  constexpr auto kMaterialPath = "/Engine/Default.omat";
  constexpr auto kTexturePath = "/Engine/Default.otex";

  auto MaterialRequirement() -> PakRequirement
  {
    return {
      .kind = KeyReferenceKind::kAsset,
      .key = AssetKey::FromVirtualPath(kMaterialPath),
      .type = AssetType::kMaterial,
    };
  }

  //! A pak of library `library` that requires `requirement` of `Engine`, or
  //! nothing.
  auto Pak(const std::string& library,
    std::vector<PakRequirement> requirements = {},
    std::vector<AssetKey> deleted = {}) -> std::shared_ptr<FakeContentSource>
  {
    const auto key = data::SourceKey { Uuid::Generate() };
    auto pak = std::make_shared<FakeContentSource>(library + ".pak", key);
    auto catalog = data::PakCatalog {
      .source_key = key,
      .content_version = 0U,
      .catalog_digest = {},
      .library = library,
      .entries = {},
      .deleted = std::move(deleted),
      .bases = {},
      .dependencies = {},
    };
    if (!requirements.empty()) {
      catalog.dependencies.push_back({
        .library = "Engine",
        .requirements = std::move(requirements),
      });
    }
    pak->SetCatalog(std::move(catalog));
    return pak;
  }

  //! A loose `Engine` layer holding the material at `material_path` as
  //! `type`.
  auto Engine(const std::string& material_path = kMaterialPath,
    const AssetType type = AssetType::kMaterial)
    -> std::shared_ptr<FakeContentSource>
  {
    auto engine = std::make_shared<FakeContentSource>(
      "Engine", data::SourceKey { Uuid::Generate() });
    static_cast<void>(engine->AddAsset(material_path, type));
    return engine;
  }

  //! Mounts `pak` above the registry's layers, returning why it was refused.
  auto Refusal(ContentSourceRegistry& registry,
    const std::shared_ptr<FakeContentSource>& pak)
    -> std::vector<FailedRequirement>
  {
    try {
      static_cast<void>(registry.MountPak(pak->SourcePath(), pak));
    } catch (const DependencyRequirementError& error) {
      return error.Failures();
    }
    return {};
  }

  NOLINT_TEST(ContentSourceRegistryRequirementTest, ALooseLayerBelowMeetsIt)
  {
    auto registry = ContentSourceRegistry {};
    const auto engine = Engine();
    static_cast<void>(registry.MountLoose(engine->DebugName(), engine));

    EXPECT_TRUE(
      Refusal(registry, Pak("Content", { MaterialRequirement() })).empty());
    EXPECT_EQ(registry.Sources().size(), 2U);
  }

  NOLINT_TEST(ContentSourceRegistryRequirementTest, MissingRefusesTheMount)
  {
    auto registry = ContentSourceRegistry {};

    const auto failures
      = Refusal(registry, Pak("Content", { MaterialRequirement() }));

    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kMissing);
    EXPECT_EQ(failures.front().library, "Engine");
    EXPECT_EQ(failures.front().requirement, MaterialRequirement());
    EXPECT_TRUE(registry.Sources().empty());
  }

  NOLINT_TEST(ContentSourceRegistryRequirementTest, AnotherTypeRefusesTheMount)
  {
    auto registry = ContentSourceRegistry {};
    const auto engine = Engine(kMaterialPath, AssetType::kGeometry);
    static_cast<void>(registry.MountLoose(engine->DebugName(), engine));

    const auto failures
      = Refusal(registry, Pak("Content", { MaterialRequirement() }));

    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kTypeMismatch);
    EXPECT_EQ(failures.front().found_type, AssetType::kGeometry);
  }

  //! The providing entry's virtual path names its library, whatever key it
  //! has.
  NOLINT_TEST(
    ContentSourceRegistryRequirementTest, AnotherLibraryRefusesTheMount)
  {
    auto registry = ContentSourceRegistry {};
    const auto other = std::make_shared<FakeContentSource>(
      "Other", data::SourceKey { Uuid::Generate() });
    const auto key
      = other->AddAsset("/Other/Default.omat", AssetType::kMaterial);
    static_cast<void>(registry.MountLoose(other->DebugName(), other));
    auto requirement = MaterialRequirement();
    requirement.key = key;

    const auto failures = Refusal(registry, Pak("Content", { requirement }));

    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kLibraryMismatch);
    EXPECT_EQ(failures.front().found_library, "Other");
  }

  //! A deletion between the pak and the providing layer is terminal.
  NOLINT_TEST(ContentSourceRegistryRequirementTest, ATombstoneRefusesTheMount)
  {
    auto registry = ContentSourceRegistry {};
    const auto engine = Engine();
    static_cast<void>(registry.MountLoose(engine->DebugName(), engine));
    const auto patch = Pak("Patch", {}, { MaterialRequirement().key });
    static_cast<void>(registry.MountPak(patch->SourcePath(), patch));

    const auto failures
      = Refusal(registry, Pak("Content", { MaterialRequirement() }));

    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kTombstoned);
  }

  NOLINT_TEST(ContentSourceRegistryRequirementTest, TexturesAreRequiredByKey)
  {
    const auto texture = PakRequirement {
      .kind = KeyReferenceKind::kTexture,
      .key = data::TextureKeyFromVirtualPath(kTexturePath),
      .type = AssetType::kUnknown,
    };
    auto registry = ContentSourceRegistry {};
    const auto engine = Engine();
    static_cast<void>(registry.MountLoose(engine->DebugName(), engine));

    const auto failures = Refusal(registry, Pak("Content", { texture }));
    ASSERT_EQ(failures.size(), 1U);
    EXPECT_EQ(failures.front().failure, RequirementFailure::kMissing);

    const auto textured = Engine();
    textured->AddTexture(kTexturePath);
    auto with_texture = ContentSourceRegistry {};
    static_cast<void>(with_texture.MountLoose(textured->DebugName(), textured));
    EXPECT_TRUE(Refusal(with_texture, Pak("Content", { texture })).empty());
  }

  //! Replacing a layer checks the paks above it against the new stack; a
  //! refused replacement leaves the old layer mounted.
  NOLINT_TEST(
    ContentSourceRegistryRequirementTest, ReplacingALayerChecksThePaksAbove)
  {
    auto registry = ContentSourceRegistry {};
    const auto engine = Engine();
    static_cast<void>(registry.MountLoose(engine->DebugName(), engine));
    const auto content = Pak("Content", { MaterialRequirement() });
    static_cast<void>(registry.MountPak(content->SourcePath(), content));
    const auto emptied
      = std::make_shared<FakeContentSource>("Engine", engine->GetSourceKey());

    NOLINT_EXPECT_THROW(
      static_cast<void>(registry.MountLoose(emptied->DebugName(), emptied)),
      DependencyRequirementError);
    EXPECT_EQ(registry.Sources().front(), engine);
  }

} // namespace
} // namespace oxygen::content::testing
