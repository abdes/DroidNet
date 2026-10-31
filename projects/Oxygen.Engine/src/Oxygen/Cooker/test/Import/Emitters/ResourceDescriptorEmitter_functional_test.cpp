//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Emitters/ResourceDescriptorEmitter.cpp

#include <fstream>
#include <memory>
#include <stdexcept>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::co::Co;
using oxygen::content::import::ImportEventLoop;
using oxygen::content::import::ResourceDescriptorEmitter;
using oxygen::content::import::WindowsFileWriter;
namespace co = oxygen::co;

namespace {

//! Fixture owning an event loop, a file writer and a temporary directory.
class WindowsFileWriterTest : public oxygen::testing::TempDirTest {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    writer_ = std::make_unique<WindowsFileWriter>(*loop_);
  }

  auto TearDown() -> void override
  {
    writer_.reset();
    loop_.reset();
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<WindowsFileWriter> writer_;
};

NOLINT_TEST_F(WindowsFileWriterTest, EmitterRetainsErrorAfterSharedWriterFlush)
{
  {
    std::ofstream blocked_directory(TempDir() / "Resources");
    blocked_directory << "not a directory";
  }
  ResourceDescriptorEmitter emitter(*writer_, TempDir());
  const oxygen::data::pak::core::TextureResourceDesc descriptor {};
  static_cast<void>(
    emitter.EmitTexture("/.cooked/Resources/Ship.glb/Paint.base_color.otex",
      oxygen::data::pak::core::ResourceIndexT { 1U }, descriptor));
  co::Run(*loop_, [&] -> Co<> {
    const auto first_flush = co_await writer_->Flush();
    EXPECT_FALSE(first_flush.has_value());
    const auto result = co_await emitter.Finalize();
    if (result) {
      ADD_FAILURE() << "Emitter discarded its write failure";
      co_return;
    }
    EXPECT_FALSE(result.error().path.empty());
    EXPECT_NE(result.error().system_error.value(), 0);
    EXPECT_EQ(
      result.error().path, oxygen::base::ToLogicalPath(result.error().path));
  });
}

//! Emitting an equal texture descriptor again records it once: the cook is
//! deterministic, so the second emit has nothing to write.
NOLINT_TEST_F(WindowsFileWriterTest, EqualDescriptorEmittedTwiceIsRecordedOnce)
{
  ResourceDescriptorEmitter emitter(*writer_, TempDir());
  const oxygen::data::pak::core::TextureResourceDesc descriptor {};
  const auto index = oxygen::data::pak::core::ResourceIndexT { 1U };
  const auto first
    = emitter.EmitTexture("/.cooked/Textures/Sky.otex", index, descriptor);
  const auto second
    = emitter.EmitTexture("/.cooked/Textures/Sky.otex", index, descriptor);

  co::Run(*loop_,
    [&] -> Co<> { EXPECT_TRUE((co_await emitter.Finalize()).has_value()); });
  EXPECT_EQ(first, second);
  EXPECT_EQ(emitter.Records().size(), 1U);
}

//! Emitting other bytes at a path already emitted is refused: two writes
//! could finish in either order, leaving bytes the record does not describe.
NOLINT_TEST_F(WindowsFileWriterTest, OtherDescriptorAtAnEmittedPathIsRefused)
{
  ResourceDescriptorEmitter emitter(*writer_, TempDir());
  const oxygen::data::pak::core::TextureResourceDesc descriptor {};
  static_cast<void>(emitter.EmitTexture("/.cooked/Textures/Sky.otex",
    oxygen::data::pak::core::ResourceIndexT { 1U }, descriptor));

  EXPECT_THROW(
    static_cast<void>(emitter.EmitTexture("/.cooked/Textures/Sky.otex",
      oxygen::data::pak::core::ResourceIndexT { 2U }, descriptor)),
    std::runtime_error);
  co::Run(*loop_,
    [&] -> Co<> { EXPECT_TRUE((co_await emitter.Finalize()).has_value()); });
}

} // namespace
