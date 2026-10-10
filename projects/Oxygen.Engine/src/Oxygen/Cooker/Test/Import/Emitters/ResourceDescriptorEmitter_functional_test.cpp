//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Emitters/ResourceDescriptorEmitter.cpp

#include <fstream>
#include <memory>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Import/FileError.h>
#include <Oxygen/Cooker/Import/IAsyncFileWriter.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/Import/Internal/WindowsFileWriter.h>
#include <Oxygen/Cooker/Loose/LooseCookedLayout.h>
#include <Oxygen/Cooker/Test/Support/TempDir.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Testing/GTest.h>

using namespace oxygen::content::import;
using namespace oxygen::co;
namespace co = oxygen::co;

namespace {

//! Fixture owning an event loop, a file writer and a temporary directory.
class WindowsFileWriterTest : public oxygen::cooker::test::TempDirTest {
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
  const LooseCookedLayout layout;
  {
    std::ofstream blocked_directory(TempDir() / layout.resources_dir);
    blocked_directory << "not a directory";
  }
  ResourceDescriptorEmitter emitter(*writer_, layout, TempDir());
  const oxygen::data::pak::core::TextureResourceDesc descriptor {};
  static_cast<void>(emitter.EmitTexture("texture", "texture-source",
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

} // namespace
