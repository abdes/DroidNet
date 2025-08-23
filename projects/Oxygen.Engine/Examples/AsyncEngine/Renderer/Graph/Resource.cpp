//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "Resource.h"

#include <algorithm>
#include <sstream>

#include <Oxygen/Base/Logging.h>

#include "../Integration/GraphicsLayerIntegration.h"

namespace oxygen::examples::asyncsim {

// === ResourceLifetimeInfo Implementation ===

auto ResourceLifetimeInfo::OverlapsWith(const ResourceLifetimeInfo& other) const
  -> bool
{
  // Prefer explicit ordering indices when both lifetimes have them
  const bool have_indices = first_index != std::numeric_limits<uint32_t>::max()
    && other.first_index != std::numeric_limits<uint32_t>::max();
  if (have_indices) {
    return !(last_index < other.first_index || other.last_index < first_index);
  }
  // Fallback to handle id ordering
  const auto first_a = first_usage.get();
  const auto last_a = last_usage.get();
  const auto first_b = other.first_usage.get();
  const auto last_b = other.last_usage.get();
  return !(last_a < first_b || last_b < first_a);
}

auto ResourceLifetimeInfo::GetDebugString() const -> std::string
{
  std::ostringstream oss;
  oss << "Lifetime[" << first_usage.get() << " - " << last_usage.get() << "] "
      << "Usages: " << usages.size() << ", "
      << "Memory: " << memory_requirement << " bytes, "
      << "WriteConflicts: " << (has_write_conflicts ? "Yes" : "No");
  return oss.str();
}

// === ResourceMemoryPool Implementation ===

auto ResourceMemoryPool::Allocate(ResourceHandle resource, size_t size,
  size_t alignment) -> std::optional<MemoryAllocation>
{
  // Align size to requested alignment
  const auto aligned_size = (size + alignment - 1) & ~(alignment - 1);

  // Try to find existing free space
  if (const auto offset = FindBestFit(aligned_size, alignment)) {
    MemoryAllocation allocation(*offset, aligned_size, resource);
    allocations_.push_back(allocation);

    used_size_ += aligned_size;
    peak_usage_ = std::max(peak_usage_, used_size_);

    LOG_F(9, "[ResourcePool] Allocated {} bytes at offset {} for resource {}",
      aligned_size, *offset, resource.get());

    return allocation;
  }

  // Expand pool if needed
  const auto new_offset = total_size_;
  total_size_ += aligned_size;

  MemoryAllocation allocation(new_offset, aligned_size, resource);
  allocations_.push_back(allocation);

  used_size_ += aligned_size;
  peak_usage_ = std::max(peak_usage_, used_size_);

  LOG_F(9,
    "[ResourcePool] Expanded pool to {} bytes, allocated {} bytes at offset {} "
    "for resource {}",
    total_size_, aligned_size, new_offset, resource.get());

  return allocation;
}

auto ResourceMemoryPool::Free(ResourceHandle resource) -> void
{
  auto it = std::find_if(allocations_.begin(), allocations_.end(),
    [resource](const MemoryAllocation& alloc) {
      return alloc.resource == resource && alloc.is_active;
    });

  if (it != allocations_.end()) {
    it->is_active = false;
    used_size_ -= it->size;

    LOG_F(9, "[ResourcePool] Freed {} bytes for resource {}, used: {}/{}",
      it->size, resource.get(), used_size_, total_size_);

    // Coalesce adjacent free blocks
    CoalesceFreed();
  } else {
    LOG_F(WARNING, "[ResourcePool] Attempted to free unknown resource {}",
      resource.get());
  }
}

auto ResourceMemoryPool::GetDebugInfo() const -> std::string
{
  const auto active_allocations
    = std::count_if(allocations_.begin(), allocations_.end(),
      [](const MemoryAllocation& alloc) { return alloc.is_active; });

  std::ostringstream oss;
  oss << "ResourcePool[" << used_size_ << "/" << total_size_ << " bytes, "
      << "Peak: " << peak_usage_ << ", "
      << "Active: " << active_allocations << "/" << allocations_.size() << "]";
  return oss.str();
}

auto ResourceMemoryPool::FindBestFit(size_t size, size_t alignment)
  -> std::optional<size_t>
{
  // Sort allocations by offset for easier free space finding
  std::vector<std::reference_wrapper<const MemoryAllocation>> active_allocs;
  for (const auto& alloc : allocations_) {
    if (alloc.is_active) {
      active_allocs.push_back(std::cref(alloc));
    }
  }

  std::sort(active_allocs.begin(), active_allocs.end(),
    [](const MemoryAllocation& a, const MemoryAllocation& b) {
      return a.offset < b.offset;
    });

  // Look for gaps between allocations
  size_t current_offset = 0;
  for (const auto& alloc_ref : active_allocs) {
    const auto& alloc = alloc_ref.get();

    // Align current offset
    const auto aligned_offset
      = (current_offset + alignment - 1) & ~(alignment - 1);

    // Check if there's enough space before this allocation
    if (aligned_offset + size <= alloc.offset) {
      return aligned_offset;
    }

    current_offset = alloc.offset + alloc.size;
  }

  // No suitable gap found
  return std::nullopt;
}

auto ResourceMemoryPool::CoalesceFreed() -> void
{
  // Remove inactive allocations
  allocations_.erase(
    std::remove_if(allocations_.begin(), allocations_.end(),
      [](const MemoryAllocation& alloc) { return !alloc.is_active; }),
    allocations_.end());
}

// === ResourceStateTracker Implementation ===

auto ResourceStateTracker::SetInitialState(
  ResourceHandle resource, ResourceState state, uint32_t view_index) -> void
{
  const auto key = std::make_pair(resource, view_index);
  resource_states_[key] = { state, PassHandle() };

  LOG_F(9, "[StateTracker] Set initial state for resource {} view {} to {}",
    resource.get(), view_index, static_cast<uint32_t>(state));
}

auto ResourceStateTracker::RequestTransition(ResourceHandle resource,
  ResourceState new_state, PassHandle pass, uint32_t view_index) -> void
{
  const auto key = std::make_pair(resource, view_index);
  auto it = resource_states_.find(key);

  if (it == resource_states_.end()) {
    // First usage, assume Common state
    SetInitialState(resource, ResourceState::Common, view_index);
    it = resource_states_.find(key);
  }

  const auto current_state = it->second.current_state;
  if (current_state != new_state) {
    // Plan transition
    ResourceTransition transition(
      resource, current_state, new_state, pass, view_index);
    planned_transitions_.push_back(transition);

    // Update current state
    it->second.current_state = new_state;
    it->second.last_used_pass = pass;

    LOG_F(9,
      "[StateTracker] Planned transition for resource {} view {} from {} to {} "
      "at pass {}",
      resource.get(), view_index, static_cast<uint32_t>(current_state),
      static_cast<uint32_t>(new_state), pass.get());
  }
}

auto ResourceStateTracker::GetCurrentState(ResourceHandle resource,
  uint32_t view_index) const -> std::optional<ResourceState>
{
  const auto key = std::make_pair(resource, view_index);
  const auto it = resource_states_.find(key);

  if (it != resource_states_.end()) {
    return it->second.current_state;
  }

  return std::nullopt;
}

auto ResourceStateTracker::Reset() -> void
{
  resource_states_.clear();
  planned_transitions_.clear();

  LOG_F(9, "[StateTracker] Reset all state tracking");
}

auto ResourceStateTracker::GetDebugInfo() const -> std::string
{
  std::ostringstream oss;
  oss << "StateTracker[" << resource_states_.size() << " resources, "
      << planned_transitions_.size() << " transitions]";
  return oss.str();
}

// === Enhanced ResourceAliasValidator with AsyncEngine integration ===

class AsyncEngineResourceAliasValidator : public ResourceAliasValidator {
public:
  explicit AsyncEngineResourceAliasValidator(
    GraphicsLayerIntegration* graphics_integration) noexcept
    : graphics_integration_(graphics_integration)
  {
  }

  auto AddResource(ResourceHandle handle, const ResourceDesc& desc)
    -> void override
  {
    ResourceLifetimeInfo info;
    info.memory_requirement = CalculateMemoryRequirement(desc);
    resource_lifetimes_[handle] = std::move(info);
    resource_descriptors_[handle] = &desc;

    LOG_F(9, "[ResourceValidator] Added resource {} with {} bytes requirement",
      handle.get(), resource_lifetimes_[handle].memory_requirement);
  }

  auto AddResourceUsage(ResourceHandle resource, PassHandle pass,
    ResourceState state, bool is_write, uint32_t view_index) -> void override
  {
    auto it = resource_lifetimes_.find(resource);
    if (it == resource_lifetimes_.end()) {
      // Suppress spam for obvious debug-fill / uninitialized patterns
      constexpr uint32_t kDebugFill = 0xBEBEBEBE; // MSVC debug pattern
      if (resource.get() == kDebugFill) {
        static bool reported = false;
        if (!reported) {
          LOG_F(4,
            "[ResourceValidator] Detected debug-fill resource handle "
            "0xBEBEBEBE; suppressing further warnings (pass={})",
            pass.get());
          reported = true;
        }
        return; // ignore usage
      }
      LOG_F(WARNING,
        "[ResourceValidator] Usage added for unknown resource {} (pass={})",
        resource.get(), pass.get());
      return;
    }

    auto& lifetime = it->second;
    ResourceUsage usage(pass, state, is_write, view_index);
    lifetime.usages.push_back(usage);

    // Update first/last usage
    // Determine ordering using provided topological order if available
    const auto current_index_it = topological_order_.find(pass);
    const uint32_t current_index = current_index_it != topological_order_.end()
      ? current_index_it->second
      : static_cast<uint32_t>(pass.get()); // fallback to handle id

    if (lifetime.usages.size() == 1) {
      lifetime.first_usage = pass;
      lifetime.last_usage = pass;
      first_usage_index_[resource] = current_index;
      last_usage_index_[resource] = current_index;
      lifetime.first_index = current_index;
      lifetime.last_index = current_index;
    } else {
      auto& first_index = first_usage_index_[resource];
      auto& last_index = last_usage_index_[resource];
      if (current_index < first_index) {
        first_index = current_index;
        lifetime.first_usage = pass;
        lifetime.first_index = current_index;
      }
      if (current_index > last_index) {
        last_index = current_index;
        lifetime.last_usage = pass;
        lifetime.last_index = current_index;
      }
    }

    // Check for write conflicts
    if (is_write) {
      for (const auto& existing_usage : lifetime.usages) {
        if (existing_usage.pass == pass
          && existing_usage.view_index == view_index
          && existing_usage.is_write_access && &existing_usage != &usage) {
          lifetime.has_write_conflicts = true;
          break;
        }
      }
    }

    LOG_F(9,
      "[ResourceValidator] Added usage for resource {} in pass {} (write: {})",
      resource.get(), pass.get(), is_write);
  }

  auto AnalyzeLifetimes() -> void override
  {
    LOG_F(2, "[ResourceValidator] Analyzing lifetimes for {} resources",
      resource_lifetimes_.size());

    // Find potential aliasing candidates
    for (auto& [handle_a, lifetime_a] : resource_lifetimes_) {
      for (auto& [handle_b, lifetime_b] : resource_lifetimes_) {
        if (handle_a >= handle_b)
          continue; // Avoid duplicates and self-comparison

        // Check if resources can be aliased
        if (CanAlias(handle_a, handle_b, lifetime_a, lifetime_b)) {
          lifetime_a.aliases.push_back(handle_b);
          lifetime_b.aliases.push_back(handle_a);

          LOG_F(9, "[ResourceValidator] Resources {} and {} can alias",
            handle_a.get(), handle_b.get());
        }
      }
    }
  }

  auto SetTopologicalOrder(
    const std::unordered_map<PassHandle, uint32_t>& order) -> void override
  {
    topological_order_ = order;
  }

  [[nodiscard]] auto GetLifetimeInfo(ResourceHandle handle) const
    -> const ResourceLifetimeInfo* override
  {
    const auto it = resource_lifetimes_.find(handle);
    return it != resource_lifetimes_.end() ? &it->second : nullptr;
  }

  [[nodiscard]] auto ValidateAliasing() -> std::vector<AliasHazard> override
  {
    std::vector<AliasHazard> hazards;

    // Enhanced validation with AsyncEngine integration
    if (graphics_integration_) {
      // Validate integration state consistency
      if (!graphics_integration_->ValidateIntegrationState()) {
        AliasHazard hazard;
        hazard.description = "Graphics layer integration state is inconsistent";
        hazards.push_back(std::move(hazard));
      }

      // Check for resource lifetime conflicts with deferred reclaimer
      const auto stats = graphics_integration_->GetIntegrationStats();
      if (stats.pending_reclaims > 0) {
        LOG_F(2,
          "[ResourceValidator] {} pending resource reclaims detected "
          "during aliasing validation",
          stats.pending_reclaims);
      }
    }

    // Validate resource aliasing hazards
    for (const auto& [handle, lifetime] : resource_lifetimes_) {
      for (const auto& alias_handle : lifetime.aliases) {
        const auto alias_it = resource_lifetimes_.find(alias_handle);
        if (alias_it == resource_lifetimes_.end())
          continue;

        const auto& alias_lifetime = alias_it->second;

        // Check for lifetime overlap hazards
        if (lifetime.OverlapsWith(alias_lifetime)) {
          AliasHazard hazard;
          hazard.resource_a = handle;
          hazard.resource_b = alias_handle;
          hazard.description = "Aliased resources have overlapping lifetimes";

          // Find conflicting passes
          for (const auto& usage_a : lifetime.usages) {
            for (const auto& usage_b : alias_lifetime.usages) {
              if (usage_a.pass == usage_b.pass) {
                hazard.conflicting_passes.push_back(usage_a.pass);
              }
            }
          }

          hazards.push_back(std::move(hazard));
        }

        // Detect write-after-write or write-after-read within overlapping usage
        // windows per view
        for (const auto& usage_a : lifetime.usages) {
          for (const auto& usage_b : alias_lifetime.usages) {
            if (usage_a.pass == usage_b.pass)
              continue; // already handled above or same pass
            if (usage_a.view_index != usage_b.view_index)
              continue; // different view can be relaxed later
            const bool overlap
              = true; // placeholder: refine with real time ordering
            // TODO(Phase2): Derive overlap from topological order + lifetime
            // first/last indices instead of placeholder
            if (!overlap)
              continue;
            // Hazard if both write, or one writes and lifetimes overlap for
            // alias
            if ((usage_a.is_write_access && usage_b.is_write_access)
              || (usage_a.is_write_access != usage_b.is_write_access)) {
              AliasHazard hazard;
              hazard.resource_a = handle;
              hazard.resource_b = alias_handle;
              hazard.description
                = usage_a.is_write_access && usage_b.is_write_access
                ? "Write/Write hazard between aliased resources"
                : "Read/Write hazard between aliased resources";
              hazard.conflicting_passes.push_back(usage_a.pass);
              hazard.conflicting_passes.push_back(usage_b.pass);
              hazards.push_back(std::move(hazard));
            }
          }
        }
      }
    }

    return hazards;
  }

  [[nodiscard]] auto GetDebugInfo() const -> std::string override
  {
    if (!graphics_integration_) {
      return "AsyncEngineResourceAliasValidator (no graphics integration)";
    }

    const auto stats = graphics_integration_->GetIntegrationStats();
    return "AsyncEngineResourceAliasValidator - "
           "Resources: "
      + std::to_string(stats.active_resources)
      + ", Descriptors: " + std::to_string(stats.allocated_descriptors)
      + ", Pending: " + std::to_string(stats.pending_reclaims);
  }

private:
  GraphicsLayerIntegration* graphics_integration_;
  std::unordered_map<ResourceHandle, ResourceLifetimeInfo> resource_lifetimes_;
  std::unordered_map<ResourceHandle, const ResourceDesc*> resource_descriptors_;
  std::unordered_map<PassHandle, uint32_t> topological_order_;
  std::unordered_map<ResourceHandle, uint32_t> first_usage_index_;
  std::unordered_map<ResourceHandle, uint32_t> last_usage_index_;

  //! Calculate memory requirement for a resource descriptor
  [[nodiscard]] auto CalculateMemoryRequirement(const ResourceDesc& desc) const
    -> size_t
  {
    // Simple estimation - real implementation would query graphics backend
    if (desc.GetTypeInfo() == "TextureDesc") {
      const auto& tex_desc = static_cast<const TextureDesc&>(desc);
      return static_cast<size_t>(tex_desc.width) * tex_desc.height
        * 4; // Assume 4 bytes per pixel
    } else if (desc.GetTypeInfo() == "BufferDesc") {
      const auto& buf_desc = static_cast<const BufferDesc&>(desc);
      return buf_desc.size_bytes;
    }
    return 0;
  }

  //! Check if two resources can be aliased
  [[nodiscard]] auto CanAlias(ResourceHandle handle_a, ResourceHandle handle_b,
    const ResourceLifetimeInfo& lifetime_a,
    const ResourceLifetimeInfo& lifetime_b) const -> bool
  {
    // Check descriptor compatibility
    const auto desc_a_it = resource_descriptors_.find(handle_a);
    const auto desc_b_it = resource_descriptors_.find(handle_b);

    if (desc_a_it == resource_descriptors_.end()
      || desc_b_it == resource_descriptors_.end()) {
      return false;
    }

    if (!AreCompatible(*desc_a_it->second, *desc_b_it->second)) {
      return false;
    }

    // Check lifetime overlap
    if (lifetime_a.OverlapsWith(lifetime_b)) {
      return false;
    }

    // Check for write conflicts
    if (lifetime_a.has_write_conflicts || lifetime_b.has_write_conflicts) {
      return false;
    }

    return true;
  }
};

//! Factory function to create AsyncEngine-integrated validator
auto CreateAsyncEngineResourceValidator(GraphicsLayerIntegration* integration)
  -> std::unique_ptr<ResourceAliasValidator>
{
  return std::make_unique<AsyncEngineResourceAliasValidator>(integration);
}

} // namespace oxygen::examples::asyncsim
