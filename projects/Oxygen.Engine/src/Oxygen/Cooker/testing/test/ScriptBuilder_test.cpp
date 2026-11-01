//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/Loaders/ScriptLoader.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Cooker/testing/ScriptBuilder.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/AssetReferences.h>
#include <Oxygen/Data/CookedSource.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::MemoryFileSystem;
using oxygen::cooker::testing::ScriptBuilder;
using oxygen::cooker::testing::SeedBytes;
namespace cooked = oxygen::content::cooked;
namespace data = oxygen::data;

//! The script the product loader reads from `bytes`, checking `references`.
auto Load(const std::span<const std::byte> bytes,
  const data::AssetReferences& references) -> std::unique_ptr<data::ScriptAsset>
{
  auto stream = oxygen::serio::ReadOnlyMemoryStream { bytes };
  auto reader = oxygen::serio::Reader<oxygen::serio::ReadOnlyMemoryStream> {
    stream,
  };
  return oxygen::content::loaders::LoadScriptAsset({
    .current_asset_key = {},
    .desc_reader = &reader,
    .asset_references = oxygen::observer_ptr { &references },
    .work_offline = true,
    .parse_only = true,
  });
}

//! A script's payloads are rows of its root's script table, named through its
//! references; its parameters and external source read back.
NOLINT_TEST(ScriptBuilderTest, ScriptReadsBackItsPayloadsParametersAndSource)
{
  auto game = LooseCookedRoot { "Game" };
  const auto bytecode = game.AddScript(SeedBytes(1));
  const auto source = game.AddScript(SeedBytes(2));
  auto script = ScriptBuilder { "Spin" };
  script.Bytecode(bytecode)
    .Source(source)
    .ExternalSource("/Game/Scripts/Spin.lua")
    .Parameters({ { "speed", data::ScriptParam { 2.0F } } });
  const auto key = game.Script("Scripts/Spin.oscript", script);

  auto storage = MemoryFileSystem {};
  const auto folder = std::filesystem::path("/cooked/root");
  storage.AddFiles(folder, game.Files());
  const auto content = cooked::CookedContent::Open(storage,
    data::CookedSource {
      .kind = data::CookedSourceKind::kLooseCooked,
      .path = folder,
    });
  const auto references = script.References();
  const auto read = Load(content.Descriptor(key), references);

  EXPECT_EQ(read->GetAssetName(), "Spin");
  EXPECT_EQ(read->GetExternalSourcePath(), "/Game/Scripts/Spin.lua");
  EXPECT_TRUE(read->HasParameter("speed"));
  EXPECT_EQ(read->ParametersCount(), 1U);
  EXPECT_EQ(references
              .ResolveResource(
                read->GetBytecodeResourceIndex(), data::ResourceKind::kScript)
              .value(),
    bytecode);
  EXPECT_EQ(references
              .ResolveResource(
                read->GetSourceResourceIndex(), data::ResourceKind::kScript)
              .value(),
    source);
}

//! A script with no payload names no resource, as a cook writes a script whose
//! source is only external.
NOLINT_TEST(ScriptBuilderTest, ScriptWithoutPayloadReferencesNothing)
{
  const auto script = ScriptBuilder { "Empty" };

  const auto references = script.References();
  const auto read = Load(script.Bytes(), references);

  EXPECT_EQ(read->GetBytecodeResourceIndex(), data::kNoResourceReference);
  EXPECT_EQ(read->GetSourceResourceIndex(), data::kNoResourceReference);
  EXPECT_FALSE(read->TryGetExternalSourcePath().has_value());
}

//! A parameter the format cannot record is refused where it is built.
NOLINT_TEST(ScriptBuilderTest, RefusesAParameterWithoutAKey)
{
  auto script = ScriptBuilder { "Broken" };
  script.Parameters({ { "", data::ScriptParam { 1.0F } } });

  EXPECT_THROW(static_cast<void>(script.Bytes()), std::invalid_argument);
}

} // namespace
