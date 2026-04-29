//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <deque>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include <fmt/format.h>

#include <Oxygen/Vortex/Internal/AuxiliaryDependencyGraph.h>
#include <Oxygen/Vortex/Internal/CompositionViewImpl.h>

namespace oxygen::vortex::internal {

namespace {

  struct AuxOutputIdHash {
    auto operator()(const CompositionView::AuxOutputId id) const noexcept
      -> std::size_t
    {
      return static_cast<std::size_t>(id.get());
    }
  };

  struct Producer {
    std::size_t packet_index { 0U };
    CompositionView::AuxOutputDesc desc {};
  };

} // namespace

auto AuxiliaryDependencyGraph::Build(std::span<const FrameViewPacket> packets)
  -> Plan
{
  auto plan = Plan {};
  plan.resolved_inputs_by_packet.resize(packets.size());
  plan.ordered_packet_indices.reserve(packets.size());

  auto producers
    = std::unordered_map<CompositionView::AuxOutputId, Producer, AuxOutputIdHash> {};
  for (std::size_t packet_index = 0; packet_index < packets.size();
    ++packet_index) {
    const auto& packet = packets[packet_index];
    for (const auto& output : packet.ProducedAuxOutputs()) {
      if (output.id.get() == 0U) {
        throw std::runtime_error(fmt::format(
          "Auxiliary output from view '{}' uses invalid AuxOutputId 0",
          packet.View().GetDescriptor().name));
      }

      const auto [it, inserted] = producers.emplace(
        output.id, Producer { .packet_index = packet_index, .desc = output });
      if (!inserted) {
        const auto& previous = packets[it->second.packet_index];
        throw std::runtime_error(fmt::format(
          "Duplicate auxiliary producer for AuxOutputId {}: '{}' and '{}'",
          output.id.get(), previous.View().GetDescriptor().name,
          packet.View().GetDescriptor().name));
      }
    }
  }

  auto outgoing = std::vector<std::vector<std::size_t>>(packets.size());
  auto indegree = std::vector<std::size_t>(packets.size(), 0U);

  for (std::size_t consumer_index = 0; consumer_index < packets.size();
    ++consumer_index) {
    const auto& consumer = packets[consumer_index];
    for (const auto& input : consumer.ConsumedAuxOutputs()) {
      if (input.id.get() == 0U) {
        throw std::runtime_error(fmt::format(
          "Auxiliary input from view '{}' uses invalid AuxOutputId 0",
          consumer.View().GetDescriptor().name));
      }

      const auto producer_it = producers.find(input.id);
      if (producer_it == producers.end()) {
        if (input.required) {
          throw std::runtime_error(fmt::format(
            "Required auxiliary input {} for view '{}' has no producer",
            input.id.get(), consumer.View().GetDescriptor().name));
        }

        plan.resolved_inputs_by_packet[consumer_index].push_back(
          AuxiliaryResolvedInput {
            .input = input,
            .kind = input.kind,
            .producer_view_id = kInvalidViewId,
            .producer_packet_index = 0U,
            .valid = false,
            .debug_name = fmt::format("Vortex.Aux[{}].Invalid.Optional",
              input.id.get()),
          });
        continue;
      }

      const auto& producer = producer_it->second;
      if (producer.desc.kind != input.kind) {
        throw std::runtime_error(fmt::format(
          "Auxiliary input {} for view '{}' expects kind {} but producer '{}' "
          "publishes kind {}",
          input.id.get(), consumer.View().GetDescriptor().name,
          static_cast<unsigned>(input.kind),
          packets[producer.packet_index].View().GetDescriptor().name,
          static_cast<unsigned>(producer.desc.kind)));
      }

      plan.resolved_inputs_by_packet[consumer_index].push_back(
        AuxiliaryResolvedInput {
          .input = input,
          .kind = producer.desc.kind,
          .producer_view_id = packets[producer.packet_index].PublishedViewId(),
          .producer_packet_index
          = static_cast<std::uint32_t>(producer.packet_index),
          .valid = true,
          .debug_name = fmt::format("Vortex.Aux[{}].{}",
            input.id.get(), producer.desc.debug_name),
        });

      if (producer.packet_index != consumer_index) {
        outgoing[producer.packet_index].push_back(consumer_index);
        ++indegree[consumer_index];
      }
    }
  }

  auto ready = std::deque<std::size_t> {};
  for (std::size_t packet_index = 0; packet_index < packets.size();
    ++packet_index) {
    if (indegree[packet_index] == 0U) {
      ready.push_back(packet_index);
    }
  }

  while (!ready.empty()) {
    const auto packet_index = ready.front();
    ready.pop_front();
    plan.ordered_packet_indices.push_back(packet_index);

    for (const auto consumer_index : outgoing[packet_index]) {
      --indegree[consumer_index];
      if (indegree[consumer_index] == 0U) {
        ready.push_back(consumer_index);
      }
    }
  }

  if (plan.ordered_packet_indices.size() != packets.size()) {
    throw std::runtime_error(
      "Cycle detected in auxiliary view dependency graph");
  }

  return plan;
}

} // namespace oxygen::vortex::internal
