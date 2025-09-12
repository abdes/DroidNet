//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <atomic>
#include <functional>
#include <optional>
#include <utility>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Composition/TypedObject.h>
#include <Oxygen/Graphics/Common/Concepts.h>
#include <Oxygen/Graphics/Common/DescriptorHandle.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>

namespace oxygen::graphics::testing {

// Minimal test resource and view description
struct TestViewDesc {
  ResourceViewType view_type { ResourceViewType::kConstantBuffer };
  DescriptorVisibility visibility { DescriptorVisibility::kShaderVisible };
  uint64_t id { 0 };

  // Required for hash and equality comparison
  // NOLINTNEXTLINE(*-unneeded-member-function)
  auto operator==(const TestViewDesc& other) const -> bool
  {
    return id == other.id && view_type == other.view_type
      && visibility == other.visibility;
  }
};
} // namespace oxygen::graphics::testing

template <> struct std::hash<oxygen::graphics::testing::TestViewDesc> {
  auto operator()(
    const oxygen::graphics::testing::TestViewDesc& v) const noexcept
    -> std::size_t
  {
    std::size_t h = std::hash<int> {}(static_cast<int>(v.view_type));
    oxygen::HashCombine(h, static_cast<int>(v.visibility));
    oxygen::HashCombine(h, v.id);
    return h;
  }
};

namespace oxygen::graphics::testing {

class FakeResource final : public RegisteredResource, public oxygen::Object {
  OXYGEN_TYPED(FakeResource)
public:
  using ViewDescriptionT = TestViewDesc;

  using GetNativeViewFn = std::function<NativeView(
    const DescriptorHandle&, const ViewDescriptionT&)>;

  FakeResource() noexcept
    : instance_id_(s_next_instance_id_.fetch_add(1u, std::memory_order_relaxed))
  {
  }

  // Use a custom behavior lambda. If set, this will be invoked for each
  // GetNativeView call.
  auto WithViewBehavior(GetNativeViewFn fn) & -> FakeResource&
  {
    behavior_ = std::move(fn);
    return *this;
  }
  auto WithViewBehavior(GetNativeViewFn fn) && -> FakeResource
  {
    // Forward to lvalue overload to avoid duplicating logic.
    static_cast<FakeResource&>(*this).WithViewBehavior(std::move(fn));
    return std::move(*this);
  }

  // Convenience presets that replicate the legacy classes' behavior.
  auto WithDefaultView() & -> FakeResource&
  {
    behavior_ = MakeDefaultBehavior();
    return *this;
  }
  auto WithDefaultView() && -> FakeResource
  {
    static_cast<FakeResource&>(*this).WithDefaultView();
    return std::move(*this);
  }

  auto WithInvalidView() & -> FakeResource&
  {
    behavior_ = MakeInvalidViewBehavior();
    return *this;
  }
  auto WithInvalidView() && -> FakeResource
  {
    static_cast<FakeResource&>(*this).WithInvalidView();
    return std::move(*this);
  }

  auto WithThrowingView(std::optional<uint64_t> id) & -> FakeResource&
  {
    throw_on_id_ = id;
    behavior_ = MakeThrowOnIdBehavior();
    return *this;
  }
  auto WithThrowingView(std::optional<uint64_t> id) && -> FakeResource
  {
    static_cast<FakeResource&>(*this).WithThrowingView(id);
    return std::move(*this);
  }

  //! Required by resource registry
  [[nodiscard]] auto GetNativeView(const DescriptorHandle& view_handle,
    const ViewDescriptionT& desc) -> NativeView
  {
    ++call_count_;
    last_desc_ = desc;
    if (!behavior_) {
      behavior_ = MakeDefaultBehavior();
    }
    return behavior_(view_handle, desc);
  }

  // Inspection helpers for tests
  auto CallCount() const noexcept { return call_count_; }
  auto LastDesc() const { return last_desc_; }

private:
  GetNativeViewFn behavior_ {};
  std::optional<uint64_t> throw_on_id_ {};
  int call_count_ { 0 };
  std::optional<ViewDescriptionT> last_desc_ {};
  uint64_t instance_id_ { 0 };

  // Per-class instance counter used to produce unique native view handles that
  // differ between FakeResource instances while still being deterministic
  // per-test. Inline static avoids ODR-definition in a translation unit. More
  // deterministic across runs and will not vary with ASLR (Address Space Layout
  // Randomization).
  inline static std::atomic<uint64_t> s_next_instance_id_ { 1 };

  // Bits to shift instance id when composing a 64-bit synthetic handle.
  static constexpr unsigned kInstanceShift = 32;

  // Helper factories to avoid duplicating lambda bodies
  auto MakeDefaultBehavior() -> GetNativeViewFn
  {
    return [instance_copy = instance_id_](const DescriptorHandle&,
             const ViewDescriptionT& desc) -> NativeView {
      // Use HashCombine helper to combine the per-instance salt and the view
      // id. This preserves the full 64-bit value on 64-bit platforms while
      // remaining deterministic across runs.
      auto handle = instance_copy;
      oxygen::HashCombine(handle, desc.id);
      return { handle, FakeResource::ClassTypeId() };
    };
  }

  static auto MakeInvalidViewBehavior() -> GetNativeViewFn
  {
    return [](const DescriptorHandle&, const ViewDescriptionT&) -> NativeView {
      return {};
    };
  }

  auto MakeThrowOnIdBehavior() -> GetNativeViewFn
  {
    // Capture a copy of the configured id so the lambda does not reference
    // the containing object's storage (which may be moved/destroyed).
    return [id_copy = throw_on_id_](const DescriptorHandle&,
             const ViewDescriptionT& desc) -> NativeView {
      if (id_copy.has_value() && desc.id == *id_copy) {
        throw std::runtime_error("FakeResource: GetNativeView forced throw");
      }
      return { desc.id, FakeResource::ClassTypeId() };
    };
  }
};

} // namespace oxygen::graphics::testing
