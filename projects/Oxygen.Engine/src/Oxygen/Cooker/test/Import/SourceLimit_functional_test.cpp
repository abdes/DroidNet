//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Cooker/Import/ImportLimits.h, Cooker/Import/IAsyncFileReader.h,
// Cooker/Import/Internal/Utils/SourceReadDiagnostics.h (source size limit)

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <Oxygen/Base/Result.h>
#include <Oxygen/Base/Uuid.h>
#include <Oxygen/Cooker/Import/AsyncImportService.h>
#include <Oxygen/Cooker/Import/IAsyncFileReader.h>
#include <Oxygen/Cooker/Import/ImportDiagnostics.h>
#include <Oxygen/Cooker/Import/ImportManifest.h>
#include <Oxygen/Cooker/Import/ImportReport.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/Utils/SourceReadDiagnostics.h>
#include <Oxygen/Cooker/Import/ModelImportState.h>
#include <Oxygen/Cooker/Import/SurfaceProvenance.h>
#include <Oxygen/Cooker/test/Support/Diagnostics.h>
#include <Oxygen/Cooker/test/Support/FileIo.h>
#include <Oxygen/Cooker/test/Support/ImportHarness.h>
#include <Oxygen/Cooker/test/Support/TestPaths.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Error.h>
#include <Oxygen/Serio/Types.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace oxygen::content::import::test {
namespace {
  using nlohmann::json;
  using oxygen::cooker::test::DiagnosticSummary;
  using oxygen::cooker::test::HasDiagnosticCode;
  using oxygen::cooker::test::ImportServiceTest;
  using oxygen::cooker::test::ReadText;
  using oxygen::cooker::test::WriteBytes;
  using oxygen::cooker::test::WriteText;

  //! A limit small enough to test both sides of without large files.
  constexpr auto kLimit = serio::ByteCount { 4096 };
  constexpr auto kModelSurfaceIdentity
    = std::string_view { "0196f2a0-5b4c-7d3e-8f10-2a3b4c5d6e80" };
  constexpr auto kPlaceholderUsed
    = std::string_view { "texture.placeholder_used" };
  constexpr auto kFoundByName
    = std::string_view { "import.texture.found_by_name" };

  //! `count` bytes of a recognizable pattern.
  auto Bytes(const std::size_t count) -> std::vector<std::byte>
  {
    auto bytes = std::vector<std::byte>(count);
    for (std::size_t i = 0; i < count; ++i) {
      bytes.at(i) = static_cast<std::byte>(i % 251U);
    }
    return bytes;
  }

  //=== Reader ===------------------------------------------------------------//

  //! A reader limited to `kLimit` over a real temporary folder.
  class LimitedReaderTest : public oxygen::testing::TempDirTest {
  protected:
    auto SetUp() -> void override
    {
      loop_ = std::make_unique<ImportEventLoop>();
      reader_ = CreateAsyncFileReader(loop_->Driver(), kLimit);
    }

    auto TearDown() -> void override
    {
      reader_.reset();
      loop_.reset();
    }

    //! A file of `count` bytes in the temporary folder.
    auto File(const std::string_view name, const std::size_t count)
      -> std::filesystem::path
    {
      auto path = TempDir() / name;
      WriteBytes(path, Bytes(count));
      return path;
    }

    auto Read(const std::filesystem::path& path, const ReadOptions options = {})
      -> Result<serio::ByteBuffer>
    {
      return co::Run(*loop_, reader_->ReadFile(path, options));
    }

    [[nodiscard]] auto Reader() const -> const IAsyncFileReader&
    {
      return *reader_;
    }

  private:
    std::unique_ptr<ImportEventLoop> loop_;
    std::unique_ptr<IAsyncFileReader> reader_;
  };

  //! A file exactly at the limit is read whole.
  NOLINT_TEST_F(LimitedReaderTest, FileAtTheLimitIsRead)
  {
    const auto path = File("at-limit.bin", kLimit.get());

    const auto read = Read(path);

    ASSERT_HAS_VALUE(read);
    EXPECT_EQ(read->Size(), kLimit);
    EXPECT_EQ(Reader().SourceLimit(), kLimit);
  }

  //! A file one byte past the limit is refused, not clipped.
  NOLINT_TEST_F(LimitedReaderTest, FileOneBytePastTheLimitIsTooLarge)
  {
    const auto path = File("past-limit.bin", kLimit.get() + 1U);

    const auto read = Read(path);

    ASSERT_FALSE(read);
    EXPECT_EQ(read.error(), serio::Error::kTooLarge);
  }

  //! A range within the limit reads from a file larger than the limit; a
  //! range past the limit is refused, even when the file holds it.
  NOLINT_TEST_F(LimitedReaderTest, RangeIsBoundedByTheLimitNotTheFile)
  {
    const auto path = File("large.bin", kLimit.get() * 3U);
    const auto bytes = Bytes(kLimit.get() * 3U);

    const auto within = Read(path,
      ReadOptions {
        .offset = kLimit.get(),
        .max_bytes = kLimit.get(),
      });
    const auto past = Read(path,
      ReadOptions {
        .offset = 1U,
        .max_bytes = kLimit.get() + 1U,
      });

    ASSERT_HAS_VALUE(within);
    ASSERT_EQ(within->Size(), kLimit);
    EXPECT_EQ(within->Bytes().front(), bytes.at(kLimit.get()));
    ASSERT_FALSE(past);
    EXPECT_EQ(past.error(), serio::Error::kTooLarge);
  }

  //=== Diagnostic ===--------------------------------------------------------//

  //! The diagnostic names the source, its size and the limit; other read
  //! failures keep their own diagnostics.
  NOLINT_TEST_F(LimitedReaderTest, DiagnosticNamesTheSizeAndTheLimit)
  {
    const auto path = File("named.bin", kLimit.get() + 7U);

    const auto too_large = detail::SourceTooLargeDiagnostic(
      make_error_code(serio::Error::kTooLarge), path, kLimit);
    const auto other = detail::SourceTooLargeDiagnostic(
      make_error_code(serio::Error::kNotFound), path, kLimit);

    ASSERT_HAS_VALUE(too_large);
    EXPECT_EQ(too_large->code, detail::kSourceTooLargeCode);
    EXPECT_EQ(too_large->severity, ImportSeverity::kError);
    EXPECT_THAT(too_large->message, ::testing::HasSubstr(path.string()));
    EXPECT_THAT(too_large->message,
      ::testing::HasSubstr(std::to_string(kLimit.get() + 7U)));
    EXPECT_THAT(
      too_large->message, ::testing::HasSubstr(std::to_string(kLimit.get())));
    EXPECT_FALSE(other.has_value());
  }

  //=== Manifest ===----------------------------------------------------------//

  //! A manifest sets the limit, past `uint32_t`; zero is refused.
  NOLINT_TEST_F(LimitedReaderTest, ManifestSetsTheLimit)
  {
    const auto large = TempDir() / "large.import.json";
    const auto zero = TempDir() / "zero.import.json";
    WriteText(
      large, R"({"version":1,"jobs":[],"max_source_bytes":5000000000})");
    WriteText(zero, R"({"version":1,"jobs":[],"max_source_bytes":0})");

    auto large_errors = std::ostringstream {};
    const auto loaded = ImportManifest::Load(large, {}, large_errors);
    auto zero_errors = std::ostringstream {};
    const auto refused = ImportManifest::Load(zero, {}, zero_errors);

    ASSERT_HAS_VALUE(loaded) << large_errors.str();
    ASSERT_HAS_VALUE(loaded->max_source_bytes);
    EXPECT_EQ(*loaded->max_source_bytes, serio::ByteCount { 5'000'000'000U });
    EXPECT_FALSE(refused.has_value());
  }

  //=== Import ===------------------------------------------------------------//

  //! An import service whose sources are limited to `kLimit`.
  class SourceLimitImportTest : public ImportServiceTest {
  protected:
    [[nodiscard]] auto ServiceConfig() const
      -> AsyncImportService::Config override
    {
      auto config = ImportServiceTest::ServiceConfig();
      config.max_source_bytes = kLimit;
      return config;
    }

    //! A model request for the glTF document `document`, written into the
    //! source folder as `model.gltf`.
    [[nodiscard]] auto ModelRequest(const json& document) const -> ImportRequest
    {
      const auto source = TempPath("source/model.gltf");
      WriteText(source, document.dump());
      auto request = ImportRequest {};
      request.source_path = source;
      request.asset_virtual_path = "/Cooked/model.gltf";
      request.source_mount_path
        = oxygen::cooker::test::SourceMountPath(request.source_path);
      request.cooked_root = CookedRoot();
      const auto identity = Uuid::FromString(kModelSurfaceIdentity);
      if (identity.has_value()) {
        request.surface_provenance
          = std::make_shared<const SurfaceProvenance>(*identity);
      }
      request.model_import = ModelImportInputs {};
      return request;
    }

    [[nodiscard]] static auto Triangle() -> json
    {
      return json::parse(ReadText(
        oxygen::cooker::test::ModelPath("static_scalar_triangle.gltf")));
    }
  };

  //! An external glTF buffer past the limit fails the import with the source
  //! size diagnostic, before cgltf allocates it.
  NOLINT_TEST_F(SourceLimitImportTest, ExternalBufferPastTheLimitFailsTheModel)
  {
    auto document = Triangle();
    auto& buffer = document["buffers"].at(1);
    const auto padded = static_cast<std::size_t>(kLimit.get()) + 1U;
    buffer["uri"] = "large.bin";
    buffer["byteLength"] = padded;
    auto request = ModelRequest(document);
    WriteBytes(request.source_path.parent_path() / "large.bin", Bytes(padded));

    const auto report = Import(std::move(request));

    ASSERT_HAS_VALUE(report);
    EXPECT_FALSE(report->success);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, detail::kSourceTooLargeCode))
      << DiagnosticSummary(report->diagnostics);
  }

  //! A model texture past the limit fails with the source size diagnostic: no
  //! file found by name stands in, and no placeholder replaces it.
  NOLINT_TEST_F(SourceLimitImportTest, ModelTexturePastTheLimitHasNoStandIn)
  {
    auto document = Triangle();
    document["images"] = json::array({ { { "uri", "large.png" } } });
    document["textures"] = json::array({ { { "source", 0 } } });
    document["materials"].at(0)["pbrMetallicRoughness"]["baseColorTexture"]
      = { { "index", 0 } };
    auto request = ModelRequest(document);
    WriteBytes(request.source_path.parent_path() / "large.png",
      Bytes(static_cast<std::size_t>(kLimit.get()) + 1U));

    const auto report = Import(std::move(request));

    ASSERT_HAS_VALUE(report);
    EXPECT_TRUE(
      HasDiagnosticCode(report->diagnostics, detail::kSourceTooLargeCode))
      << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(HasDiagnosticCode(report->diagnostics, kPlaceholderUsed))
      << DiagnosticSummary(report->diagnostics);
    EXPECT_FALSE(HasDiagnosticCode(report->diagnostics, kFoundByName))
      << DiagnosticSummary(report->diagnostics);
  }

} // namespace
} // namespace oxygen::content::import::test
