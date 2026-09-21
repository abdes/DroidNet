//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>

namespace oxygen::graphics {

//! Outcome of recording submission, distinct from GPU execution completion.
enum class SubmissionOutcome : uint8_t {
  kSubmitted,
  kDiscarded,
};

[[nodiscard]] inline auto to_string(const SubmissionOutcome outcome) noexcept
  -> std::string_view
{
  switch (outcome) {
  case SubmissionOutcome::kSubmitted:
    return "Submitted";
  case SubmissionOutcome::kDiscarded:
    return "Discarded";
  }
  return "__NotSupported__";
}

//! Move-only ownership of a CPU submission observer, consumable by C++20
//! clients.
/*!
 Small nothrow-movable captures stay inline. Larger, over-aligned, or
 potentially throwing-move captures have one unique allocation. Moving a
 callback transfers its sole owner without invoking it; destruction releases
 captures exactly once. Invocation exceptions propagate to CommandRecorder's
 observer isolation boundary.
*/
class SubmissionCallback final {
public:
  SubmissionCallback() noexcept = default;

  template <typename Callable>
    requires(!std::same_as<std::remove_cvref_t<Callable>, SubmissionCallback>
      && std::is_invocable_r_v<void, std::decay_t<Callable>&,
        SubmissionOutcome>)
  explicit SubmissionCallback(Callable&& callable)
  {
    using Target = std::decay_t<Callable>;
    if constexpr (std::is_pointer_v<Target>) {
      if (callable == nullptr) {
        return;
      }
    }
    if constexpr (kFitsInline<Target>) {
      target_ = std::construct_at(static_cast<Target*>(InlineAddress()),
        std::forward<Callable>(callable));
    } else {
      target_
        = std::make_unique<Target>(std::forward<Callable>(callable)).release();
    }
    operations_ = &kOperationsFor<Target>;
  }

  ~SubmissionCallback() noexcept { Reset(); }
  SubmissionCallback(const SubmissionCallback&) = delete;
  auto operator=(const SubmissionCallback&) -> SubmissionCallback& = delete;

  SubmissionCallback(SubmissionCallback&& other) noexcept { MoveFrom(other); }
  auto operator=(SubmissionCallback&& other) noexcept -> SubmissionCallback&
  {
    if (this != &other) {
      Reset();
      MoveFrom(other);
    }
    return *this;
  }

  [[nodiscard]] explicit operator bool() const noexcept
  {
    return operations_ != nullptr;
  }

  auto operator()(const SubmissionOutcome outcome) -> void
  {
    if (operations_ == nullptr) {
      throw std::bad_function_call {};
    }
    operations_->invoke(target_, outcome);
  }

private:
  // Covers the renderer's small publication captures without shared ownership
  // or a heap allocation per observer.
  static constexpr size_t kInlineCapacity = 8U * sizeof(void*);
  template <typename Target>
  static constexpr bool kFitsInline = sizeof(Target) <= kInlineCapacity
    && alignof(Target) <= alignof(std::max_align_t)
    && std::is_nothrow_move_constructible_v<Target>;

  struct InlineMove {
    void* source;
    void* destination;
  };

  using InlineMover = void (*)(InlineMove) noexcept;

  template <typename Target>
  [[nodiscard]] static constexpr auto MakeInlineMover() noexcept -> InlineMover
  {
    if constexpr (kFitsInline<Target>) {
      return [](const InlineMove move) noexcept -> void {
        std::construct_at(static_cast<Target*>(move.destination),
          std::move(*static_cast<Target*>(move.source)));
        std::destroy_at(static_cast<Target*>(move.source));
      };
    } else {
      return nullptr;
    }
  }

  struct Operations {
    void (*invoke)(void*, SubmissionOutcome);
    void (*destroy)(void*) noexcept;
    InlineMover move_inline;
  };

  template <typename Target>
  static constexpr auto kOperationsFor = Operations {
    .invoke = [](void* target, const SubmissionOutcome outcome) -> void {
      std::invoke(*static_cast<Target*>(target), outcome);
    },
    .destroy = [](void* target) noexcept -> void {
      if constexpr (kFitsInline<Target>) {
        std::destroy_at(static_cast<Target*>(target));
      } else {
        const auto owner
          = std::unique_ptr<Target>(static_cast<Target*>(target));
      }
    },
    .move_inline = MakeInlineMover<Target>(),
  };

  [[nodiscard]] auto InlineAddress() noexcept -> void*
  {
    return storage_.data();
  }

  auto Reset() noexcept -> void
  {
    if (operations_ != nullptr) {
      operations_->destroy(target_);
      operations_ = nullptr;
      target_ = nullptr;
    }
  }

  auto MoveFrom(SubmissionCallback& other) noexcept -> void
  {
    operations_ = std::exchange(other.operations_, nullptr);
    if (operations_ == nullptr) {
      return;
    }
    if (operations_->move_inline != nullptr) {
      target_ = InlineAddress();
      operations_->move_inline(
        { .source = other.target_, .destination = target_ });
    } else {
      target_ = other.target_;
    }
    other.target_ = nullptr;
  }

  alignas(std::max_align_t) std::array<std::byte, kInlineCapacity> storage_ {};
  const Operations* operations_ { nullptr };
  void* target_ { nullptr };
};

} // namespace oxygen::graphics
