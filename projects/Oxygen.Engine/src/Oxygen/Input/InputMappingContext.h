//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include <Oxygen/Core/Time/Types.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Input/api_export.h>

namespace oxygen {

namespace platform {
  class InputEvent;
  class InputSlot;
} // namespace platform

namespace input {
  class InputActionMapping;

  //! A named, ordered set of mappings activated and prioritized as a unit.
  /*!
   ### Consumption

   Consumption applies to physical input, not to whole contexts. A mapping
   whose action consumes input and triggers from an input event claims the
   physical sources its slot reads: a key or mouse button; the mouse X and/or
   Y axis (MouseXY reads both); the wheel X and/or Y axis (MouseWheelY covers
   Up and Down, MouseWheelX covers Left and Right); for AnyKey, the key or
   button of the event. Later mappings in the same context and mappings in
   lower-priority contexts that read a claimed source do not receive that
   input: an active one is canceled, an idle one has its staged input dropped
   without an edge. Mappings on other sources update normally.
  */
  class InputMappingContext {
  public:
    //! Physical sources claimed during one evaluation pass.
    using ClaimedSources = std::vector<const platform::InputSlot*>;

    //! A context identified by `asset_key`; the name is a label that other
    //! contexts may share.
    /*!
     A context hydrated from an input mapping context asset carries the
     asset's key; a context created in code is made by `MakeCodeContext`
     (CodeInput.h), which gives it a code key.

     @throw std::invalid_argument when `asset_key` is nil.
    */
    OXGN_NPUT_API InputMappingContext(
      std::string name, data::AssetKey asset_key);

    OXGN_NPUT_API void AddMapping(std::shared_ptr<InputActionMapping> mapping);

    [[nodiscard]] auto GetName() const -> const std::string& { return name_; }

    //! The context's identity; never nil.
    [[nodiscard]] auto GetAssetKey() const noexcept -> const data::AssetKey&
    {
      return asset_key_;
    }

    [[nodiscard]] auto GetMappings() const
      -> const std::vector<std::shared_ptr<InputActionMapping>>&
    {
      return mappings_;
    }

    //! Route an input event to the mappings whose slot reads it.
    OXGN_NPUT_API void HandleInput(
      const platform::InputSlot& slot, const platform::InputEvent& event) const;

    //! Evaluate all mappings for one pass.
    /*!
     @param delta_time Time advanced by time-based triggers.
     @param claimed Physical sources claimed by higher-priority mappings in
     this pass; sources claimed by this context are appended.
     @param event_slot Slot of the input event that started this pass, or
     nullptr for the per-frame time pass. Resolves AnyKey.
     @return true if a mapping of this context consumed input.
    */
    OXGN_NPUT_API auto Update(oxygen::time::CanonicalDuration delta_time,
      ClaimedSources& claimed,
      const platform::InputSlot* event_slot = nullptr) const -> bool;

    //! Evaluate all mappings in isolation; returns true if any consumed.
    OXGN_NPUT_NDAPI auto Update(
      oxygen::time::CanonicalDuration delta_time) const -> bool;

    //! End every activation driven by this context and withdraw its
    //! contributions. Used when the context stops receiving input.
    OXGN_NPUT_API void DeactivateAll() const;

  private:
    std::string name_;
    data::AssetKey asset_key_;
    std::vector<std::shared_ptr<InputActionMapping>> mappings_;
  };

} // namespace input
} // namespace oxygen
