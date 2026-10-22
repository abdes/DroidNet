//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "pch.h"

#include <EditorModule/SurfaceRegistry.h>
#include <Oxygen/Graphics/Common/Surface.h>

using namespace Microsoft::VisualStudio::TestTools::UnitTesting;
using oxygen::graphics::Surface;
using oxygen::graphics::Texture;
using oxygen::interop::module::SurfaceRegistry;

namespace {

//! A surface with no swap chain: the registry only stores and hands it over.
class FakeSurface final : public Surface {
public:
  FakeSurface()
    : Surface("Fake Surface")
  {
  }

  auto Resize() -> void override { }
  [[nodiscard]] auto GetCurrentBackBufferIndex() const -> uint32_t override
  {
    return 0;
  }
  [[nodiscard]] auto GetCurrentBackBuffer() const
    -> std::shared_ptr<Texture> override
  {
    return {};
  }
  [[nodiscard]] auto GetBackBuffer(uint32_t /*index*/) const
    -> std::shared_ptr<Texture> override
  {
    return {};
  }
  auto Present() const -> void override { }
  [[nodiscard]] auto Width() const -> uint32_t override { return 1; }
  [[nodiscard]] auto Height() const -> uint32_t override { return 1; }
};

constexpr SurfaceRegistry::GuidKey kViewport { 1, 2, 3, 4 };

//! The outcome a callback was given, if it ran.
struct Outcome {
  bool called { false };
  bool ok { false };

  auto Callback() -> std::function<void(bool)>
  {
    return [this](const bool result) {
      called = true;
      ok = result;
    };
  }
};

} // namespace

namespace InteropTests {

//! The registry stages surface registrations and removals for the engine
//! thread; a removal and a registration for one viewport may overlap.
[TestClass]
public ref class SurfaceRegistryTests {
public:
  [TestMethod]
  void RemovingALiveSurfaceStagesItsDestruction()
  {
    SurfaceRegistry registry;
    const auto surface = std::make_shared<FakeSurface>();
    registry.CommitRegistration(kViewport, surface);
    Outcome removed;

    registry.RemoveSurface(kViewport, removed.Callback());

    Assert::IsTrue(registry.FindSurface(kViewport) == nullptr);
    auto destructions = registry.DrainPendingDestructions();
    Assert::AreEqual(std::size_t { 1 }, destructions.size());
    Assert::IsTrue(destructions[0].second.first == surface);
    destructions[0].second.second(true);
    Assert::IsTrue(removed.called && removed.ok);
  }

  [TestMethod]
  void RemovingAnUnknownSurfaceFailsAtOnce()
  {
    SurfaceRegistry registry;
    Outcome removed;

    registry.RemoveSurface(kViewport, removed.Callback());

    Assert::IsTrue(removed.called && !removed.ok);
    Assert::IsTrue(registry.DrainPendingDestructions().empty());
  }

  //! An attach cancelled before the engine committed its surface: the
  //! surface is destroyed instead of committed, and the registration fails.
  [TestMethod]
  void RemovalWithdrawsAPendingRegistration()
  {
    SurfaceRegistry registry;
    const auto surface = std::make_shared<FakeSurface>();
    Outcome registered;
    Outcome removed;
    registry.RegisterSurface(kViewport, surface, registered.Callback());

    registry.RemoveSurface(kViewport, removed.Callback());

    Assert::IsTrue(registry.DrainPendingRegistrations().empty());
    auto destructions = registry.DrainPendingDestructions();
    Assert::AreEqual(std::size_t { 1 }, destructions.size());
    Assert::IsTrue(destructions[0].second.first == surface);
    Assert::IsFalse(removed.called, L"the removal completes when the engine destroys the surface");
    destructions[0].second.second(true);
    Assert::IsTrue(registered.called && !registered.ok);
    Assert::IsTrue(removed.called && removed.ok);
  }

  //! A commit over a live surface for the same viewport retires the replaced
  //! surface, so that it and what the engine keeps for the viewport go.
  [TestMethod]
  void CommitOverALiveSurfaceRetiresIt()
  {
    SurfaceRegistry registry;
    const auto replaced = std::make_shared<FakeSurface>();
    const auto current = std::make_shared<FakeSurface>();
    registry.CommitRegistration(kViewport, replaced);

    registry.CommitRegistration(kViewport, current);

    Assert::IsTrue(registry.FindSurface(kViewport) == current);
    const auto destructions = registry.DrainPendingDestructions();
    Assert::AreEqual(std::size_t { 1 }, destructions.size());
    Assert::IsTrue(destructions[0].second.first == replaced);
  }
};

} // namespace InteropTests
