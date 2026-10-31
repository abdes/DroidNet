//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using oxygen::content::PakFile;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::SourceKeyOf;
using oxygen::data::CookedOpenError;
using oxygen::data::CookedOpenFailure;
namespace core = oxygen::data::pak::core;

//! A valid pak of one material.
auto ValidPak() -> std::vector<std::byte>
{
  auto game = LooseCookedRoot { "Game" };
  static_cast<void>(game.Material("Materials/Wood.omat"));
  return CookedPak { game }.Encode();
}

//! Opens `bytes` as a pak, as a loader does.
auto OpenPak(const std::vector<std::byte>& bytes) -> std::unique_ptr<PakFile>
{
  return std::make_unique<PakFile>(
    oxygen::serio::AnyDevice(
      oxygen::serio::MemoryFile::FromBytes(bytes).value()),
    "defect.pak");
}

//! The failure opening `bytes` as a pak reports.
auto PakFailure(const std::vector<std::byte>& bytes) -> CookedOpenFailure
{
  try {
    static_cast<void>(OpenPak(bytes));
  } catch (const CookedOpenError& error) {
    return error.Failure();
  }
  throw std::logic_error("the pak opened");
}

struct PakDefect final {
  std::string name;
  std::function<void(std::vector<std::byte>&)> apply;
  CookedOpenFailure failure;
};

class CookedBytesPakTest : public ::testing::TestWithParam<PakDefect> { };

//! Each pak defect makes the pak fail to open, for its own reason.
NOLINT_TEST_P(CookedBytesPakTest, DefectFailsForItsReason)
{
  auto bytes = ValidPak();

  GetParam().apply(bytes);

  EXPECT_EQ(PakFailure(bytes), GetParam().failure);
}

INSTANTIATE_TEST_SUITE_P(Defects, CookedBytesPakTest,
  ::testing::Values(
    PakDefect {
      .name = "NewerVersion",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetPakVersion(
          bytes, static_cast<uint16_t>(core::kCurrentPakFormatVersion + 1U));
      },
      .failure = CookedOpenFailure::kUnsupportedVersion,
    },
    PakDefect {
      .name = "HeaderMagic",
      .apply = &CookedBytes::BreakPakMagic,
      .failure = CookedOpenFailure::kWrongFormat,
    },
    PakDefect {
      .name = "FooterMagic",
      .apply = &CookedBytes::BreakFooterMagic,
      .failure = CookedOpenFailure::kCorrupt,
    },
    PakDefect {
      .name = "HeaderIdentity",
      .apply = [](std::vector<std::byte>& bytes) -> void {
        CookedBytes::SetHeaderIdentity(bytes, SourceKeyOf(0xEEU));
      },
      .failure = CookedOpenFailure::kCorrupt,
    },
    PakDefect {
      .name = "NoBrowseIndex",
      .apply = &CookedBytes::DropBrowseIndex,
      .failure = CookedOpenFailure::kCorrupt,
    },
    PakDefect {
      .name = "NoCatalog",
      .apply = &CookedBytes::DropCatalog,
      .failure = CookedOpenFailure::kCorrupt,
    },
    PakDefect {
      .name = "DirectoryOutside",
      .apply = &CookedBytes::MoveDirectoryOutside,
      .failure = CookedOpenFailure::kCorrupt,
    },
    PakDefect {
      .name = "CatalogOverrun",
      .apply = &CookedBytes::OverrunCatalog,
      .failure = CookedOpenFailure::kCorrupt,
    }),
  [](const ::testing::TestParamInfo<PakDefect>& info) -> std::string {
    return info.param.name;
  });

//! A stale CRC opens, and fails only the integrity check.
NOLINT_TEST(CookedBytesTest, CorruptCrcFailsOnlyTheIntegrityCheck)
{
  auto bytes = ValidPak();

  CookedBytes::CorruptCrc(bytes);

  const auto pak = OpenPak(bytes);
  EXPECT_THROW(pak->ValidateCrc32Integrity(), std::exception);
}

//! The CRC field lies in the footer, at the field the footer declares.
NOLINT_TEST(CookedBytesTest, CrcFieldIsTheFootersField)
{
  const auto bytes = ValidPak();

  EXPECT_EQ(CookedBytes::CrcFieldOffset(bytes),
    bytes.size() - sizeof(core::PakFooter)
      + offsetof(core::PakFooter, pak_crc32));
}

//! The failure loading the index of `files` reports.
auto IndexFailure(oxygen::cooker::testing::LooseCookedFiles& files)
  -> CookedOpenFailure
{
  try {
    static_cast<void>(oxygen::content::lc::LooseCookedIndex::LoadFromBytes(
      CookedBytes::Index(files)));
  } catch (const CookedOpenError& error) {
    return error.Failure();
  }
  throw std::logic_error("the index loaded");
}

//! An index of another version is refused as such.
NOLINT_TEST(CookedBytesTest, IndexVersionIsUnsupported)
{
  auto game = LooseCookedRoot { "Game" };
  static_cast<void>(game.Material("Materials/Wood.omat"));
  auto files = game.Files();

  CookedBytes::SetIndexVersion(files,
    static_cast<uint16_t>(oxygen::data::loose_cooked::kIndexVersion + 1U));

  EXPECT_EQ(IndexFailure(files), CookedOpenFailure::kUnsupportedVersion);
}

//! An index of another magic is not an index.
NOLINT_TEST(CookedBytesTest, IndexMagicIsWrongFormat)
{
  auto game = LooseCookedRoot { "Game" };
  static_cast<void>(game.Material("Materials/Wood.omat"));
  auto files = game.Files();

  CookedBytes::BreakIndexMagic(files);

  EXPECT_EQ(IndexFailure(files), CookedOpenFailure::kWrongFormat);
}

} // namespace
