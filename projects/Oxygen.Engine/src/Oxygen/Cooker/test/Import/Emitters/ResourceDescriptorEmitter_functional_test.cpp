//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Import/Internal/Emitters/ResourceDescriptorEmitter.cpp

#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Import/Internal/Emitters/ResourceDescriptorEmitter.h>
#include <Oxygen/Cooker/Import/Internal/ImportEventLoop.h>
#include <Oxygen/Cooker/testing/MemoryFileSystem.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/Serio/IoDriver.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

using oxygen::co::Co;
using oxygen::content::import::ImportEventLoop;
using oxygen::content::import::ResourceDescriptorEmitter;
namespace co = oxygen::co;

namespace {

//! Fixture owning an event loop and a temporary directory, for the one test
//! that publishes real files.
class ResourceDescriptorEmitterRealFileTest
  : public oxygen::testing::TempDirTest {
protected:
  auto SetUp() -> void override { loop_ = std::make_unique<ImportEventLoop>(); }

  auto TearDown() -> void override
  {
    emitter_.reset();
    loop_.reset();
  }

  //! Runs `body(emitter)` in a coroutine whose nursery receives the emitter's
  //! writes; the emitter publishes real files under `TempDir()` and stays for
  //! inspection afterwards.
  template <typename Body> auto RunEmitter(Body body) -> void
  {
    co::Run(*loop_, [&] -> Co<> {
      OXCO_WITH_NURSERY(writes)
      {
        auto& emitter
          = emitter_.emplace(oxygen::content::cooked::RealCookerStorage(),
            loop_->Driver(), writes, TempDir());
        co_await body(emitter);
        co_return co::kJoin;
      };
    });
  }

  std::unique_ptr<ImportEventLoop> loop_;
  std::optional<ResourceDescriptorEmitter> emitter_;
};

//! Fixture owning an event loop and a file system held in memory, with an
//! inline driver.
class ResourceDescriptorEmitterTest : public ::testing::Test {
protected:
  auto SetUp() -> void override
  {
    loop_ = std::make_unique<ImportEventLoop>();
    driver_ = std::make_unique<oxygen::serio::IoDriver>(
      oxygen::serio::IoDriver::CreateInline().value());
  }

  auto TearDown() -> void override
  {
    emitter_.reset();
    driver_.reset();
    loop_.reset();
  }

  //! Runs `body(emitter)` in a coroutine whose nursery receives the emitter's
  //! writes; the emitter publishes to the in-memory file system and stays for
  //! inspection afterwards.
  template <typename Body> auto RunEmitter(Body body) -> void
  {
    co::Run(*loop_, [&] -> Co<> {
      OXCO_WITH_NURSERY(writes)
      {
        auto& emitter = emitter_.emplace(files_, *driver_, writes, "/cooked");
        co_await body(emitter);
        co_return co::kJoin;
      };
    });
  }

  oxygen::cooker::testing::MemoryFileSystem files_;
  std::unique_ptr<ImportEventLoop> loop_;
  std::unique_ptr<oxygen::serio::IoDriver> driver_;
  std::optional<ResourceDescriptorEmitter> emitter_;
};

//! On the real file system: a write the storage refuses is the error
//! `Finalize` returns, and the emitter keeps it.
NOLINT_TEST_F(
  ResourceDescriptorEmitterRealFileTest, FinalizeReturnsTheWriteFailure)
{
  {
    std::ofstream blocked_directory(TempDir() / "Resources");
    blocked_directory << "not a directory";
  }
  RunEmitter([&](ResourceDescriptorEmitter& emitter) -> Co<> {
    const oxygen::data::pak::core::TextureResourceDesc descriptor {};
    static_cast<void>(
      emitter.EmitTexture("/.cooked/Resources/Ship.glb/Paint.base_color.otex",
        oxygen::data::pak::core::ResourceIndexT { 1U }, descriptor));
    const auto result = co_await emitter.Finalize();
    if (result) {
      ADD_FAILURE() << "Emitter discarded its write failure";
      co_return;
    }
    EXPECT_NE(result.error().error.value(), 0);
    EXPECT_EQ(result.error().path.filename(), "Paint.base_color.otex");
  });
}

//! Emitting an equal texture descriptor again records it once: the cook is
//! deterministic, so the second emit has nothing to write.
NOLINT_TEST_F(
  ResourceDescriptorEmitterTest, EqualDescriptorEmittedTwiceIsRecordedOnce)
{
  std::string first;
  std::string second;
  RunEmitter([&](ResourceDescriptorEmitter& emitter) -> Co<> {
    const oxygen::data::pak::core::TextureResourceDesc descriptor {};
    const auto index = oxygen::data::pak::core::ResourceIndexT { 1U };
    first
      = emitter.EmitTexture("/.cooked/Textures/Sky.otex", index, descriptor);
    second
      = emitter.EmitTexture("/.cooked/Textures/Sky.otex", index, descriptor);

    EXPECT_TRUE(co_await emitter.Finalize());
  });
  EXPECT_EQ(first, second);
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_EQ(emitter_->Records().size(), 1U);
}

//! Emitting other bytes at a path already emitted is refused: two writes
//! could finish in either order, leaving bytes the record does not describe.
NOLINT_TEST_F(
  ResourceDescriptorEmitterTest, OtherDescriptorAtAnEmittedPathIsRefused)
{
  RunEmitter([&](ResourceDescriptorEmitter& emitter) -> Co<> {
    const oxygen::data::pak::core::TextureResourceDesc descriptor {};
    static_cast<void>(emitter.EmitTexture("/.cooked/Textures/Sky.otex",
      oxygen::data::pak::core::ResourceIndexT { 1U }, descriptor));

    EXPECT_THROW(
      static_cast<void>(emitter.EmitTexture("/.cooked/Textures/Sky.otex",
        oxygen::data::pak::core::ResourceIndexT { 2U }, descriptor)),
      std::runtime_error);
    EXPECT_TRUE(co_await emitter.Finalize());
  });
}

//! A buffer descriptor path the index cannot hold, one leaving the cooked root
//! or rooted, is refused before anything is written.
NOLINT_TEST_F(
  ResourceDescriptorEmitterTest, BufferPathLeavingTheRootIsRefusedUnwritten)
{
  RunEmitter([&](ResourceDescriptorEmitter& emitter) -> Co<> {
    const oxygen::data::pak::core::BufferResourceDesc descriptor {
      .size_bytes = 4U,
    };
    const auto index = oxygen::data::pak::core::ResourceIndexT { 1U };
    for (const auto* const relpath :
      { "../outside.obuf", "/rooted.obuf", "Buffers/../../outside.obuf" }) {
      EXPECT_THROW(static_cast<void>(
                     emitter.EmitBufferAtRelPath(relpath, index, descriptor)),
        std::runtime_error);
    }

    EXPECT_TRUE(co_await emitter.Finalize());
  });
  ASSERT_HAS_VALUE(emitter_);
  EXPECT_TRUE(emitter_->Records().empty());
  EXPECT_EQ(
    files_.KindOf("/outside.obuf"), oxygen::content::cooked::PathKind::kNone);
  EXPECT_EQ(
    files_.KindOf("/rooted.obuf"), oxygen::content::cooked::PathKind::kNone);
}

} // namespace
