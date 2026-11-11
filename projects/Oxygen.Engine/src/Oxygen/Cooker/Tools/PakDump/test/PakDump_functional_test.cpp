//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Tools/PakDump/PakDumpCommand.cpp, Tools/PakDump/AssetDumpers.h,
// Tools/PakDump/DecodedAsset.h

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <ostream>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "PakDumpCommand.h"

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetValidation.h>
#include <Oxygen/Cooker/Tools/Common/ToolApp.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>
#include <Oxygen/Cooker/testing/AssetDescriptors.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/PackedPak.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::pakdump::PakDumpCommand;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::PackedPak;
using oxygen::data::AssetType;

namespace toolkit = oxygen::content::toolkit;

//! What one run of PakDump wrote, and its exit code.
struct DumpRun final {
  int code = 0;
  std::string out;
  std::string err;
};

//! Runs PakDump on paks written into the test's folder.
class PakDumpTest : public oxygen::testing::TempDirTest {
protected:
  //! Writes `bytes` as the pak `name`; returns where it is.
  [[nodiscard]] auto WritePak(const std::string_view name,
    const std::span<const std::byte> bytes) const -> std::filesystem::path
  {
    const auto path = TempPath(name);
    Materialize(bytes, path);
    return path;
  }

  //! Dumps the pak at `path`.
  [[nodiscard]] static auto Dump(const std::filesystem::path& path) -> DumpRun
  {
    auto command = PakDumpCommand {};
    const auto commands = std::array<toolkit::ToolCommand*, 1> { &command };
    const auto pak = path.string();
    const auto args = std::array<const char*, 2> {
      "Oxygen.Cooker.PakDump",
      pak.c_str(),
    };
    auto out = std::ostringstream {};
    auto err = std::ostringstream {};
    const auto code = toolkit::RunTool(
      toolkit::ToolInfo {
        .program = "Oxygen.Cooker.PakDump",
        .version = "0.1",
        .about = "PakDump under test.",
      },
      commands, args,
      toolkit::ToolStreams {
        .out = oxygen::observer_ptr<std::ostream> { &out },
        .err = oxygen::observer_ptr<std::ostream> { &err },
      });
    return { .code = code, .out = out.str(), .err = err.str() };
  }

  //! How often `needle` appears in `text`.
  [[nodiscard]] static auto Count(
    const std::string_view text, const std::string_view needle) -> size_t
  {
    auto count = size_t { 0 };
    for (auto at = text.find(needle); at != std::string_view::npos;
      at = text.find(needle, at + needle.size())) {
      ++count;
    }
    return count;
  }
};

//! A root holding one asset of each type PakDump has a dumper for and the
//! kits build: material, geometry, scene, script, physics material, input
//! action and input context.
[[nodiscard]] auto MakeEveryTypeRoot() -> LooseCookedRoot
{
  auto root = LooseCookedRoot { "Game" };
  static_cast<void>(root.Material("Wood.omat"));
  static_cast<void>(root.Geometry("Crate.ogeo"));
  static_cast<void>(root.Scene("Level.oscene"));
  static_cast<void>(root.Script("Spin.oscript"));
  static_cast<void>(root.PhysicsMaterial("Wood.opmat"));
  const auto action = root.InputAction("Move.oiact");
  auto context = oxygen::cooker::testing::InputContextBuilder { "Gameplay" };
  context.Mapping(action, "Space", {});
  static_cast<void>(root.InputContext("Gameplay.oimap", context));
  return root;
}

//! Every asset the runtime reads is dumped, none refused, none failed.
NOLINT_TEST_F(PakDumpTest, DumpsEveryAssetTheRuntimeReads)
{
  // Arrange
  const auto bytes = CookedPak { MakeEveryTypeRoot() }.Encode();
  const auto path = WritePak("every_type.pak", bytes);
  const auto asset_count = PackedPak { bytes }.Pak().Directory().size();

  // Act
  const auto run = Dump(path);

  // Assert
  EXPECT_EQ(run.code, 0) << run.err;
  EXPECT_EQ(Count(run.out, "Asset #"), asset_count) << run.out;
  EXPECT_EQ(Count(run.out, "Refused by the runtime"), 0U) << run.out;
  EXPECT_THAT(run.err, ::testing::Not(::testing::HasSubstr("ERROR")))
    << run.err;
}

//! A descriptor the runtime refuses is refused by PakDump too, which prints
//! none of its fields: a material whose flags hold a bit no version defines.
NOLINT_TEST_F(PakDumpTest, RefusesTheDescriptorTheRuntimeRefuses)
{
  // Arrange
  auto root = LooseCookedRoot { "Game" };
  const auto material = root.Material("Wood.omat");
  auto bytes = CookedPak { std::move(root) }.Encode();
  constexpr auto kUndefinedFlag = uint32_t { 1U << 31U };
  CookedBytes::SetMaterialFlags(bytes, material, kUndefinedFlag);
  const auto refused = PackedPak { bytes };
  const auto refused_entry = refused.Pak().FindEntry(material);
  ASSERT_HAS_VALUE(refused_entry);
  EXPECT_THROW(oxygen::content::ValidateAssetDescriptor(AssetType::kMaterial,
                 material, refused.Pak().DescriptorBytes(*refused_entry),
                 refused.Pak().ReadAssetReferences(material)),
    oxygen::data::CookedFormatError);
  const auto path = WritePak("refused.pak", bytes);

  // Act
  const auto run = Dump(path);

  // Assert
  EXPECT_EQ(Count(run.out, "Refused by the runtime"), 1U) << run.out;
  EXPECT_THAT(run.out, ::testing::Not(::testing::HasSubstr("Material Domain")))
    << run.out;
}

} // namespace
