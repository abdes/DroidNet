//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Content/AssetLoader.h>
#include <Oxygen/Content/ContentLoadError.h>
#include <Oxygen/Cooker/testing/Descriptors.h>
#include <Oxygen/Core/EngineTag.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Data/CookedFormatError.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
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
  //! Awaits `body` while the loader is activated and running, then stops the
  //! loader and returns what `body` returned; a test checks it with
  //! `ASSERT_HAS_VALUE` before it reads it.
  template <typename T> auto WhileActive(co::Co<T> body) -> std::optional<T>
  {
    return co::Run(loop_, Activated(std::move(body)));
  }

  co::testing::TestEventLoop loop_;
  co::ThreadPool pool_ { loop_, 1 };
  AssetLoader loader_ { engine::internal::EngineTagFactory::Get(),
    AssetLoaderConfig { .thread_pool = observer_ptr { &pool_ } } };

private:
  template <typename T>
  auto Activated(co::Co<T> body) -> co::Co<std::optional<T>>
  {
    std::optional<T> result;
    OXCO_WITH_NURSERY(nursery)
    {
      co_await nursery.Start(&AssetLoader::ActivateAsync, &loader_);
      loader_.Run();
      result.emplace(co_await std::move(body));
      loader_.Stop();
      co_return co::kJoin;
    };
    co_return result;
  }
};

//! The cooked bytes of a vertex buffer of `size` bytes of `fill`, three floats
//! per element, as `CookedResourceData<data::BufferResource>` takes them.
inline auto CookedVertexBuffer(const std::size_t size, const std::byte fill)
  -> std::vector<uint8_t>
{
  auto desc = data::pak::core::BufferResourceDesc {};
  desc.usage_flags = 1U;
  desc.element_format = static_cast<uint8_t>(Format::kRGB32Float);
  return cooker::testing::CookedResourceBytes(
    desc, std::vector<std::byte>(size, fill));
}

//! Awaits `load`, returning what it threw, or null when it completed.
template <typename T>
auto ThrownBy(co::Co<T> load) -> co::Co<std::exception_ptr>
{
  try {
    static_cast<void>(co_await std::move(load));
  } catch (...) {
    co_return std::current_exception();
  }
  co_return nullptr;
}

//! Why `error` failed a load, when it is a `ContentLoadError`.
inline auto LoadFailureOf(const std::exception_ptr& error)
  -> std::optional<ContentLoadFailure>
{
  if (!error) {
    return std::nullopt;
  }
  try {
    std::rethrow_exception(error);
  } catch (const ContentLoadError& failure) {
    return failure.Failure();
  } catch (...) {
    return std::nullopt;
  }
}

//! The defect of the first `data::CookedFormatError` in the chain of errors
//! nested in `error`, `error` included.
inline auto CookedDefectIn(const std::exception_ptr& error)
  -> std::optional<data::CookedDefect>
{
  if (!error) {
    return std::nullopt;
  }
  // The engine builds without RTTI, so the chain is walked by catching
  // `std::nested_exception`, not with `std::rethrow_if_nested`.
  try {
    std::rethrow_exception(error);
  } catch (const data::CookedFormatError& refused) {
    return refused.Defect();
  } catch (const std::nested_exception& nested) {
    return CookedDefectIn(nested.nested_ptr());
  } catch (...) {
    return std::nullopt;
  }
}

} // namespace oxygen::content::testing
