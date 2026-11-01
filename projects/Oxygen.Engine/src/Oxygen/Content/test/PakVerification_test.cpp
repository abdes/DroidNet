//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
// Covers: Content/Internal/PakVerification.h

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <vector>

#include <asio/io_context.hpp>

#include <Oxygen/Content/Internal/PakVerification.h>
#include <Oxygen/Content/PakFile.h>
#include <Oxygen/Cooker/testing/CookedBytes.h>
#include <Oxygen/Cooker/testing/CookedPak.h>
#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Cooker/testing/Seeds.h>
#include <Oxygen/Data/CookedOpenError.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h> // IWYU pragma: keep (EventLoopTraits)
#include <Oxygen/Serio/AnyDevice.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Serio/MemoryFile.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace {

using oxygen::content::PakFile;
using oxygen::content::internal::VerifyPakAsync;
using oxygen::cooker::testing::CookedBytes;
using oxygen::cooker::testing::CookedPak;
using oxygen::cooker::testing::LooseCookedRoot;
using oxygen::cooker::testing::Materialize;
using oxygen::cooker::testing::SeedBytes;
using oxygen::data::CookedOpenError;
using oxygen::data::CookedOpenFailure;

constexpr auto kMount = "Tests";
constexpr auto kTextureSeed = uint8_t { 7U };

//! A pak with one texture, so it has a payload to corrupt.
auto TexturePak() -> std::vector<std::byte>
{
  auto root = LooseCookedRoot { kMount };
  const auto texture = root.AddTexture(SeedBytes(kTextureSeed));
  static_cast<void>(root.Material("A.omat", texture));
  return CookedPak { root }.Encode();
}

//! Verifies pak files of a temp folder through an inline driver, which
//! completes every read on the awaiting thread.
class PakVerificationTest : public oxygen::testing::TempDirTest {
protected:
  //! What verifying the file `bytes` make refuses it with; empty when it
  //! passes.
  auto Verify(const std::vector<std::byte>& bytes)
    -> std::optional<CookedOpenFailure>
  {
    const auto path = TempPath("test.pak");
    Materialize(bytes, path);
    auto driver = oxygen::serio::IoDriver::CreateInline().value();
    auto file = driver.Open(path, {});
    EXPECT_TRUE(file.has_value());
    if (!file) {
      return std::nullopt;
    }
    auto failure = std::optional<CookedOpenFailure> {};
    auto loop = asio::io_context {};
    oxygen::co::Run(loop, [&] -> oxygen::co::Co<> {
      try {
        co_await VerifyPakAsync(*file, path);
      } catch (const CookedOpenError& error) {
        failure = error.Failure();
      }
    });
    return failure;
  }
};

//! A pak whose CRC32 matches its bytes passes.
NOLINT_TEST_F(PakVerificationTest, PassesAPakWithACorrectCrc)
{
  // Arrange
  const auto bytes = TexturePak();

  // Act
  const auto failure = Verify(bytes);

  // Assert
  EXPECT_FALSE(failure.has_value());
}

//! A payload byte that changed after the pak was cooked is a CRC mismatch.
NOLINT_TEST_F(PakVerificationTest, RefusesAFlippedPayloadByteAsCorrupt)
{
  // Arrange
  auto bytes = TexturePak();
  const auto payload_offset = static_cast<size_t>(
    PakFile(oxygen::serio::AnyDevice(
              oxygen::serio::MemoryFile::FromBytes(bytes).value()),
      "test.pak")
      .Footer()
      .texture_region.offset);
  bytes.at(payload_offset) ^= std::byte { 0x01 };

  // Act
  const auto failure = Verify(bytes);

  // Assert
  ASSERT_HAS_VALUE(failure);
  EXPECT_EQ(*failure, CookedOpenFailure::kCorrupt);
}

//! A pak that holds no CRC32 cannot be verified, which a verified mount
//! refuses.
NOLINT_TEST_F(PakVerificationTest, RefusesAPakWithoutCrcAsUnverifiable)
{
  // Arrange
  auto bytes = TexturePak();
  CookedBytes::SetCrc(bytes, 0U);

  // Act
  const auto failure = Verify(bytes);

  // Assert
  ASSERT_HAS_VALUE(failure);
  EXPECT_EQ(*failure, CookedOpenFailure::kUnverifiable);
}

//! A file with no room for a footer is not a pak to verify.
NOLINT_TEST_F(PakVerificationTest, RefusesAFileShorterThanAFooterAsCorrupt)
{
  // Arrange
  const auto bytes = std::vector<std::byte>(4);

  // Act
  const auto failure = Verify(bytes);

  // Assert
  ASSERT_HAS_VALUE(failure);
  EXPECT_EQ(*failure, CookedOpenFailure::kCorrupt);
}

} // namespace
