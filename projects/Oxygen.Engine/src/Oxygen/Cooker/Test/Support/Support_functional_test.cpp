//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Test/Support/TempDir.cpp, Test/Support/FileIo.cpp,
// Test/Support/TestPaths.cpp, Test/Support/JsonSchema.cpp

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include <Oxygen/Cooker/Test/Support/FileIo.h>
#include <Oxygen/Cooker/Test/Support/JsonSchema.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Cooker/Test/Support/TestPaths.h>
#include <Oxygen/Testing/GTest.h>

namespace {

using namespace oxygen::cooker::test;

NOLINT_TEST(ScopedTempDirTest, CreatesDirectoryAndRemovesItOnDestruction)
{
  auto path = std::optional<std::filesystem::path> {};
  {
    const ScopedTempDir dir;
    path = dir.Path();
    EXPECT_TRUE(std::filesystem::is_directory(*path));
  }
  EXPECT_FALSE(std::filesystem::exists(*path));
}

NOLINT_TEST(ScopedTempDirTest, TwoInstancesGetDistinctDirectories)
{
  const ScopedTempDir first;
  const ScopedTempDir second;
  EXPECT_NE(first.Path(), second.Path());
}

class FileIoTest : public TempDirTest { };

NOLINT_TEST_F(FileIoTest, BytesRoundTripThroughNestedPath)
{
  const auto bytes = std::array { std::byte { 0 }, std::byte { 0xFF },
    std::byte { 0x0A }, std::byte { 0x0D } };
  const auto path = TempPath("nested/dir/data.bin");
  WriteBytes(path, bytes);
  EXPECT_THAT(ReadBytes(path), ::testing::ElementsAreArray(bytes));
}

NOLINT_TEST_F(FileIoTest, TextRoundTripPreservesLineEndings)
{
  const auto path = TempPath("text.txt");
  WriteText(path, "a\r\nb\n");
  EXPECT_EQ(ReadText(path), "a\r\nb\n");
}

NOLINT_TEST_F(FileIoTest, ReadingMissingFileThrows)
{
  EXPECT_THROW(static_cast<void>(ReadBytes(TempDir() / "missing.bin")),
    std::runtime_error);
}

NOLINT_TEST(TestPathsTest, CheckedInModelIsFound)
{
  EXPECT_TRUE(std::filesystem::is_regular_file(ModelPath("Tabuleiro.glb")));
}

NOLINT_TEST(TestPathsTest, MissingModelThrows)
{
  EXPECT_THROW(
    static_cast<void>(ModelPath("does-not-exist.fbx")), std::runtime_error);
}

NOLINT_TEST(JsonSchemaTest, ReportsViolationWithPointer)
{
  const auto schema = nlohmann::json::parse(
    R"({"type":"object","properties":{"n":{"type":"integer"}}})");
  EXPECT_THAT(ValidateJson(schema, nlohmann::json::parse(R"({"n":1})")),
    ::testing::IsEmpty());
  EXPECT_THAT(ValidateJson(schema, nlohmann::json::parse(R"({"n":"x"})")),
    ::testing::ElementsAre(::testing::StartsWith("/n: ")));
}

} // namespace
