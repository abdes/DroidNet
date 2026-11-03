//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/OxCo/ThreadPool.h>
#include <Oxygen/OxCo/test/Utils/TestEventLoop.h>
#include <Oxygen/Testing/GTest.h>
#include <Oxygen/Testing/TempDirTest.h>

namespace oxygen::content::testing {

//! AssetLoader fixture with isolated temporary storage.
class AssetLoaderBasicTest : public oxygen::testing::TempDirTest {
protected:
  using Tag = oxygen::engine::internal::EngineTagFactory;

  auto SetUp() -> void override;
  auto TearDown() -> void override;

  std::unique_ptr<AssetLoader> asset_loader_;
};

} // namespace oxygen::content::testing

namespace oxygen::engine::internal {
struct EngineTagFactory {
  static auto Get() noexcept -> EngineTag { return EngineTag {}; }
};
} // namespace oxygen::engine::internal

namespace oxygen::content::testing {

//! A loader with a thread pool, for the operations that read off the owning
//! thread; `co::Run(loop_, ...)` drives one to its result.
class AssetLoaderPoolTest : public oxygen::testing::TempDirTest {
protected:
  co::testing::TestEventLoop loop_;
  co::ThreadPool pool_ { loop_, 1 };
  AssetLoader loader_ { engine::internal::EngineTagFactory::Get(),
    AssetLoaderConfig { .thread_pool = observer_ptr { &pool_ } } };
};

} // namespace oxygen::content::testing
