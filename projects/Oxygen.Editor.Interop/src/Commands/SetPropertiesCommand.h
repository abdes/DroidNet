//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause.
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <Commands/PropertyApplierRegistry.h>
#include <Commands/PropertyKeys.h>
#include <EditorModule/EditorCommand.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Scene/SceneNode.h>

namespace oxygen::interop::module {

  //! Generic, schema-driven property update — property pipeline §5.3.
  /*!
   Carries an opaque list of `PropertyEntry` values targeting a single
   scene node, dispatched at `PhaseId::kSceneMutation`. The command is
   schema-agnostic: at execute time it groups entries by `ComponentId`
   and hands each contiguous run to the applier resolved from the
   `PropertyApplierRegistry`. Adding new components or new properties
   does not modify this command.
  */
  class SetPropertiesCommand : public EditorCommand {
  public:
    SetPropertiesCommand(oxygen::scene::NodeHandle node,
      std::vector<PropertyEntry> entries);

    void Execute(CommandContext& context) override;

  private:
    oxygen::scene::NodeHandle node_;
    std::vector<PropertyEntry> entries_;
  };

  inline SetPropertiesCommand::SetPropertiesCommand(
    oxygen::scene::NodeHandle node, std::vector<PropertyEntry> entries)
    : EditorCommand(oxygen::core::PhaseId::kSceneMutation)
    , node_(node)
    , entries_(std::move(entries))
  {
  }

  inline void SetPropertiesCommand::Execute(CommandContext& context)
  {
    if (!context.Scene || entries_.empty()) {
      DLOG_F(WARNING,
        "SetPropertiesCommand skipped: scene={} entries={}",
        context.Scene ? "available" : "missing", entries_.size());
      return;
    }

    auto sceneNode = context.Scene->GetNode(node_);
    if (!sceneNode || !sceneNode->IsAlive()) {
      LOG_F(WARNING,
        "SetPropertiesCommand skipped: target scene node is missing or dead; entries={}",
        entries_.size());
      return;
    }

    // Group entries by ComponentId so each applier sees one contiguous
    // span. Stable sort preserves the user's authoring order within a
    // component (some appliers depend on this for last-write-wins).
    std::stable_sort(entries_.begin(), entries_.end(),
      [](const PropertyEntry& lhs, const PropertyEntry& rhs) {
        return static_cast<std::uint16_t>(lhs.component)
          < static_cast<std::uint16_t>(rhs.component);
      });

    auto& registry = PropertyApplierRegistry::Instance();
    auto begin = entries_.begin();
    while (begin != entries_.end()) {
      const auto component = begin->component;
      auto end = std::find_if(begin, entries_.end(),
        [component](const PropertyEntry& e) {
          return e.component != component;
        });

      if (auto* applier = registry.Find(component)) {
        const std::span<const PropertyEntry> run(&*begin,
          static_cast<std::size_t>(end - begin));
        applier->Apply(*sceneNode, run);
      } else {
        LOG_F(WARNING,
          "SetPropertiesCommand skipped {} entries: no applier registered "
          "for component id {}",
          static_cast<std::size_t>(end - begin),
          static_cast<std::uint16_t>(component));
      }

      begin = end;
    }
  }

} // namespace oxygen::interop::module

#pragma managed(pop)
