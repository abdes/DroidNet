//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Core/Scripting/ScriptExecutable.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Scene/api_export.h>

namespace oxygen::scene {

//! Scene component managing script slots and their runtime state.
/*!
  The `ScriptingComponent` allows multiple scripts to be attached to a single
  scene node. Each script is managed in a "slot", which contains the
  reference to the `ScriptAsset` and any runtime parameter overrides.
*/
class ScriptingComponent final : public Component {
  OXYGEN_COMPONENT(ScriptingComponent)

public:
  //! Represents a single script attachment slot.
  class Slot {
  public:
    enum class CompileState : uint8_t {
      kPendingCompilation = 0,
      kReady = 1,
      kCompilationFailed = 2,
    };

    struct Diagnostic {
      std::string message;
    };

    struct ParameterEntry {
      std::string_view key;
      std::reference_wrapper<const data::ScriptParam> value;
    };

    class EffectiveParametersView
      : public std::ranges::view_interface<EffectiveParametersView> {
    public:
      EffectiveParametersView() = default;
      explicit EffectiveParametersView(const Slot* slot) noexcept
        : slot_(slot)
      {
      }

      class Iterator {
      public:
        using iterator_category = std::input_iterator_tag;
        using value_type = ParameterEntry;
        using difference_type = std::ptrdiff_t;

        Iterator() = default;
        explicit Iterator(const Slot* slot, bool is_end) noexcept
          : slot_(slot)
          , override_it_((slot != nullptr) ? slot->overrides_.begin()
                                           : OverrideIterator {})
          , override_end_(
              (slot != nullptr) ? slot->overrides_.end() : OverrideIterator {})
          , in_overrides_(!is_end)
          , at_end_(is_end || slot == nullptr)
        {
          if (!at_end_) {
            AdvanceToVisible();
          }
        }

        auto operator*() const -> ParameterEntry
        {
          if (in_overrides_) {
            return ParameterEntry {
              .key = override_it_->first,
              .value = std::cref(override_it_->second),
            };
          }
          const auto entry = **default_it_;
          return ParameterEntry { .key = entry.key, .value = entry.value };
        }

        auto operator++() -> Iterator&
        {
          if (at_end_) {
            return *this;
          }

          if (in_overrides_) {
            ++override_it_;
          } else {
            ++(*default_it_);
          }
          AdvanceToVisible();
          return *this;
        }

        auto operator++(int) -> void { ++(*this); }

        friend auto operator==(
          const Iterator& it, std::default_sentinel_t /*sentinel*/) -> bool
        {
          return it.at_end_;
        }

      private:
        using OverrideIterator = std::vector<
          std::pair<std::string, data::ScriptParam>>::const_iterator;
        using DefaultRange
          = decltype(std::declval<const data::ScriptAsset&>().Parameters());
        using DefaultIterator = std::ranges::iterator_t<DefaultRange>;

        auto AdvanceToVisible() -> void
        {
          if (at_end_) {
            return;
          }

          if (in_overrides_ && override_it_ == override_end_) {
            EnterDefaults();
          }

          if (!in_overrides_) {
            SkipShadowedDefaults();
            if (default_it_ && default_end_ && *default_it_ == *default_end_) {
              at_end_ = true;
            }
          }
        }

        auto EnterDefaults() -> void
        {
          in_overrides_ = false;
          default_range_.reset();
          default_it_.reset();
          default_end_.reset();

          if (slot_ == nullptr || slot_->asset_ == nullptr) {
            at_end_ = true;
            return;
          }

          default_range_.emplace(slot_->asset_->Parameters());
          default_it_.emplace(std::ranges::begin(*default_range_));
          default_end_.emplace(std::ranges::end(*default_range_));
        }

        auto SkipShadowedDefaults() -> void
        {
          while (default_it_ && default_end_ && *default_it_ != *default_end_) {
            const auto entry = **default_it_;
            if (!slot_->HasOverrideParameter(entry.key)) {
              break;
            }
            ++(*default_it_);
          }
        }

        const Slot* slot_ { nullptr };
        OverrideIterator override_it_;
        OverrideIterator override_end_;
        std::optional<DefaultRange> default_range_;
        std::optional<DefaultIterator> default_it_;
        std::optional<DefaultIterator> default_end_;
        bool in_overrides_ { true };
        bool at_end_ { true };
      };

      [[nodiscard]] auto begin() const -> Iterator
      {
        return Iterator(slot_, false);
      }

      [[nodiscard]] auto end() const -> std::default_sentinel_t
      {
        return std::default_sentinel;
      }

    private:
      const Slot* slot_ { nullptr };
    };

    [[nodiscard]] auto Asset() const noexcept
      -> const std::shared_ptr<const data::ScriptAsset>&
    {
      return asset_;
    }

    [[nodiscard]] auto State() const noexcept -> CompileState
    {
      return compile_state_;
    }

    [[nodiscard]] auto Diagnostics() const noexcept
      -> std::span<const Diagnostic>
    {
      return diagnostics_;
    }

    [[nodiscard]] auto IsDisabled() const noexcept -> bool
    {
      return compile_state_ == CompileState::kCompilationFailed;
    }

    [[nodiscard]] auto Executable() const noexcept
      -> const std::shared_ptr<const scripting::ScriptExecutable>&
    {
      return executable_;
    }

    auto Run() const noexcept -> void
    {
      if (executable_) {
        executable_->Run();
      }
    }

    [[nodiscard]] auto OverrideParameters() const
    {
      return overrides_ | std::views::transform([](const auto& kv) -> auto {
        return ParameterEntry { .key = kv.first,
          .value = std::cref(kv.second) };
      });
    }

    //! Returns a view of effective parameters (overrides first, then defaults
    //! not shadowed by overrides).
    [[nodiscard]] auto Parameters() const -> EffectiveParametersView
    {
      return EffectiveParametersView(this);
    }

    [[nodiscard]] auto HasOverrideParameter(std::string_view key) const noexcept
      -> bool
    {
      return TryGetOverrideParameter(key).has_value();
    }

    [[nodiscard]] auto TryGetOverrideParameter(
      std::string_view key) const noexcept
      -> std::optional<std::reference_wrapper<const data::ScriptParam>>
    {
      const auto it = std::ranges::find_if(
        overrides_, [key](const auto& pair) { return pair.first == key; });
      if (it == overrides_.end()) {
        return std::nullopt;
      }
      return std::cref(it->second);
    }

    [[nodiscard]] auto GetOverrideParameter(std::string_view key) const
      -> const data::ScriptParam&;

  private:
    friend class ScriptingComponent;
    uint64_t slot_id_ { 0 };
    std::shared_ptr<const data::ScriptAsset> asset_;
    std::vector<std::pair<std::string, data::ScriptParam>> overrides_;
    CompileState compile_state_ { CompileState::kPendingCompilation };
    std::vector<Diagnostic> diagnostics_;
    std::shared_ptr<const scripting::ScriptExecutable> executable_;
  };

  //! Default constructor.
  OXGN_SCN_API ScriptingComponent() = default;

  //! Virtual destructor.
  OXGN_SCN_API ~ScriptingComponent() override = default;

  OXYGEN_DEFAULT_COPYABLE(ScriptingComponent)
  OXYGEN_DEFAULT_MOVABLE(ScriptingComponent)

  //! Adds a new script slot to this component.
  //! Invalidates previously obtained slot references and spans.
  OXGN_SCN_API auto AddSlot(std::shared_ptr<const data::ScriptAsset> asset)
    -> void;

  //! Removes a script slot by reference.
  //! Invalidates previously obtained slot references and spans.
  OXGN_SCN_API auto RemoveSlot(const Slot& slot) -> bool;

  //! Returns a read-only view of active script slots.
  [[nodiscard]] auto Slots() const noexcept -> std::span<const Slot>
  {
    return slots_;
  }

  //! Sets a runtime parameter override for a specific script slot.
  //! The slot reference must come from this component's current Slots() view.
  OXGN_SCN_API auto SetParameter(
    const Slot& slot, std::string_view name, data::ScriptParam value) -> bool;

  //! Tries to get a parameter value, checking overrides first, then falling
  //! back to defaults.
  //! The slot reference must come from this component's current Slots() view.
  OXGN_SCN_NDAPI auto TryGetParameter(
    const Slot& slot, std::string_view name) const
    -> std::optional<std::reference_wrapper<const data::ScriptParam>>;

  //! Gets a parameter value, checking overrides first, then falling back to
  //! defaults.
  //! Throws std::out_of_range when not found.
  OXGN_SCN_NDAPI auto GetParameter(
    const Slot& slot, std::string_view name) const -> const data::ScriptParam&;

  //! Returns effective parameters for the given slot.
  OXGN_SCN_NDAPI auto Parameters(const Slot& slot) const
    -> Slot::EffectiveParametersView;

  //! Marks a slot as ready and installs its executable implementation.
  OXGN_SCN_NDAPI auto MarkSlotReady(const Slot& slot,
    std::shared_ptr<const scripting::ScriptExecutable> executable) -> bool;

  //! Marks a slot as compilation-failed and disables execution.
  //! Failure is recorded only once per slot.
  OXGN_SCN_NDAPI auto MarkSlotCompilationFailed(
    const Slot& slot, std::string diagnostic_message) -> bool;

  //! Indicates that this component supports deep cloning.
  [[nodiscard]] auto IsCloneable() const noexcept -> bool override
  {
    return true;
  }

  //! Creates a deep clone of this component.
  OXGN_SCN_NDAPI auto Clone() const -> std::unique_ptr<Component> override;

private:
  [[nodiscard]] auto FindSlotById(uint64_t slot_id) noexcept
    -> std::vector<Slot>::iterator;
  [[nodiscard]] auto FindSlotById(uint64_t slot_id) const noexcept
    -> std::vector<Slot>::const_iterator;

  std::vector<Slot> slots_;
  uint64_t next_slot_id_ { 1 };
};

} // namespace oxygen::scene
