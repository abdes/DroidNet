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

  // Reset candidates for fresh validation run
  alias_candidates_.clear();

    // 1. Integration consistency warnings (non-fatal)
    ValidateIntegrationState(hazards);

    // 2. Pairwise hazard scan (transient / potentially aliasable resources)
    std::vector<ResourceHandle> handles;
    handles.reserve(resource_lifetimes_.size());
    for (auto const& kv : resource_lifetimes_)
      handles.push_back(kv.first);
    std::sort(handles.begin(), handles.end(),
      [](auto a, auto b) { return a.get() < b.get(); });

    for (size_t i = 0; i < handles.size(); ++i) {
      auto ha = handles[i];
      const auto* descA = GetDesc(ha);
      if (!descA)
        continue;
      for (size_t j = i + 1; j < handles.size(); ++j) {
        auto hb = handles[j];
        const auto* descB = GetDesc(hb);
        if (!descB)
          continue;

        const auto& lifeA = resource_lifetimes_[ha];
        const auto& lifeB = resource_lifetimes_[hb];

        // Only consider transient resources for alias opportunities / hazards
        const bool transient_pair
          = descA->GetLifetime() == ResourceLifetime::Transient
          && descB->GetLifetime() == ResourceLifetime::Transient;

        const bool lifetimes_overlap = lifeA.OverlapsWith(lifeB);

        bool emitted_hazard = false;

        // 2.a Lifetime overlap hazard for transient pair
        if (transient_pair && lifetimes_overlap) {
          hazards.push_back(
            MakeOverlapHazard(ha, hb, lifeA, lifeB, *descA, *descB));
          emitted_hazard = true;
        }

        // 2.b Scope conflict (Shared vs PerView) when overlapping
        if (lifetimes_overlap && descA->GetScope() != descB->GetScope()) {
          hazards.push_back(MakeScopeHazard(ha, hb, *descA, *descB));
          emitted_hazard = true; // still count as hazard, may co-exist
        }

        // 2.c Overlapping writes (write-write) – always hazardous if overlap
        if (lifetimes_overlap && HasWriteOverlap(lifeA, lifeB)) {
          hazards.push_back(
            MakeWriteConflictHazard(ha, hb, lifeA, lifeB, *descA, *descB));
          emitted_hazard = true;
        }

        // 2.d Incompatibility (non-overlapping but cannot alias due to
        // format/size)
        if (transient_pair && !lifetimes_overlap
          && !AreCompatible(*descA, *descB)) {
          hazards.push_back(MakeIncompatibilityHazard(ha, hb, *descA, *descB));
          emitted_hazard = true;
        }

        // 2.e Safe alias candidate (transient, non-overlapping, compatible, no hazards just emitted for this pair)
        if (transient_pair && !lifetimes_overlap && !emitted_hazard
          && AreCompatible(*descA, *descB)) {
          AliasCandidate cand;
          cand.resource_a = ha;
          cand.resource_b = hb;
          const auto sizeA = lifeA.memory_requirement;
          const auto sizeB = lifeB.memory_requirement;
            cand.combined_memory = std::max(sizeA, sizeB);
          cand.description = DescriptorSummary(*descA) + " <-> "
            + DescriptorSummary(*descB);
          alias_candidates_.push_back(std::move(cand));
        }
      }
    }

    return hazards;
  }

  [[nodiscard]] auto GetAliasCandidates() const
    -> std::vector<AliasCandidate> override
  {
    return alias_candidates_;
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
  std::vector<AliasCandidate> alias_candidates_;

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

  // === Hazard helper routines (decomposed for low complexity) ===

  void ValidateIntegrationState(std::vector<AliasHazard>& hazards) const
  {
    if (!graphics_integration_)
      return;
    if (!graphics_integration_->ValidateIntegrationState()) {
      AliasHazard hz;
      hz.description = "Graphics layer integration state is inconsistent";
      hazards.push_back(std::move(hz));
    }
    const auto stats = graphics_integration_->GetIntegrationStats();
    if (stats.pending_reclaims > 0) {
      LOG_F(
        3, "[ResourceValidator] Pending reclaims: {}", stats.pending_reclaims);
    }
  }

  [[nodiscard]] const ResourceDesc* GetDesc(ResourceHandle h) const
  {
    auto it = resource_descriptors_.find(h);
    return it == resource_descriptors_.end() ? nullptr : it->second;
  }

  [[nodiscard]] bool HasWriteOverlap(
    const ResourceLifetimeInfo& a, const ResourceLifetimeInfo& b) const
  {
    // Fast reject via interval overlap is assumed already done by caller
    for (auto const& ua : a.usages)
      if (ua.is_write_access)
        for (auto const& ub : b.usages)
          if (ub.is_write_access && ua.pass == ub.pass)
            return true;
    // If no same-pass writes, approximate using index windows for conservative
    // detection
    return (a.first_index <= b.last_index) && (b.first_index <= a.last_index)
      && (HasAnyWrite(a) && HasAnyWrite(b));
  }

  [[nodiscard]] bool HasAnyWrite(const ResourceLifetimeInfo& l) const
  {
    return std::any_of(l.usages.begin(), l.usages.end(),
      [](auto const& u) { return u.is_write_access; });
  }

  [[nodiscard]] std::vector<PassHandle> CollectOverlapPasses(
    const ResourceLifetimeInfo& a, const ResourceLifetimeInfo& b) const
  {
    std::vector<PassHandle> passes;
    const uint32_t begin = std::max(a.first_index, b.first_index);
    const uint32_t end = std::min(a.last_index, b.last_index);
    if (begin == std::numeric_limits<uint32_t>::max()
      || end == std::numeric_limits<uint32_t>::max())
      return passes;
    auto collect = [&](auto const& life) {
      for (auto const& u : life.usages) {
        auto it = topological_order_.find(u.pass);
        uint32_t idx
          = it != topological_order_.end() ? it->second : u.pass.get();
        if (idx >= begin && idx <= end) {
          if (std::find(passes.begin(), passes.end(), u.pass) == passes.end())
            passes.push_back(u.pass);
        }
      }
    };
    collect(a);
    collect(b);
    return passes;
  }

  [[nodiscard]] AliasHazard MakeOverlapHazard(ResourceHandle a,
    ResourceHandle b, const ResourceLifetimeInfo& la,
    const ResourceLifetimeInfo& lb, const ResourceDesc& da,
    const ResourceDesc& db) const
  {
    AliasHazard hz;
    hz.resource_a = a;
    hz.resource_b = b;
    hz.description = "Transient lifetime overlap: '" + da.GetDebugName()
      + "' vs '" + db.GetDebugName() + "'";
    hz.conflicting_passes = CollectOverlapPasses(la, lb);
  hz.severity = AliasHazard::Severity::Error;
    return hz;
  }

  [[nodiscard]] AliasHazard MakeScopeHazard(ResourceHandle a, ResourceHandle b,
    const ResourceDesc& da, const ResourceDesc& db) const
  {
    AliasHazard hz;
    hz.resource_a = a;
    hz.resource_b = b;
    hz.description = "Scope conflict (" + ScopeString(da.GetScope()) + " vs "
      + ScopeString(db.GetScope()) + ")";
  hz.severity = AliasHazard::Severity::Warning;
    return hz;
  }

  [[nodiscard]] AliasHazard MakeWriteConflictHazard(ResourceHandle a,
    ResourceHandle b, const ResourceLifetimeInfo& la,
    const ResourceLifetimeInfo& lb, const ResourceDesc& da,
    const ResourceDesc& db) const
  {
    AliasHazard hz;
    hz.resource_a = a;
    hz.resource_b = b;
    hz.description = "Overlapping write hazard: '" + da.GetDebugName() + "' & '"
      + db.GetDebugName() + "'";
    hz.conflicting_passes = CollectOverlapPasses(la, lb);
  hz.severity = AliasHazard::Severity::Error;
    return hz;
  }

  [[nodiscard]] AliasHazard MakeIncompatibilityHazard(ResourceHandle a,
    ResourceHandle b, const ResourceDesc& da, const ResourceDesc& db) const
  {
    AliasHazard hz;
    hz.resource_a = a;
    hz.resource_b = b;
    hz.description = "Incompatible for aliasing: " + DescriptorSummary(da)
      + " vs " + DescriptorSummary(db);
    hz.severity = AliasHazard::Severity::Warning;
    return hz;
  }

  static std::string ScopeString(ResourceScope scope)
  {
    switch (scope) {
    case ResourceScope::Shared:
      return "Shared";
    case ResourceScope::PerView:
      return "PerView";
    }
    return "Unknown";
  }

  static std::string DescriptorSummary(const ResourceDesc& d)
  {
    std::ostringstream oss;
    if (d.GetTypeInfo() == "TextureDesc") {
      auto const& td = static_cast<const TextureDesc&>(d);
      oss << "Tex['" << d.GetDebugName() << "' " << td.width << "x" << td.height
          << " fmt=" << static_cast<uint32_t>(td.format) << " use="
          << static_cast<uint32_t>(td.usage) << "]";
    } else if (d.GetTypeInfo() == "BufferDesc") {
      auto const& bd = static_cast<const BufferDesc&>(d);
      oss << "Buf['" << d.GetDebugName() << "' size=" << bd.size_bytes
          << " stride=" << bd.stride << " use="
          << static_cast<uint32_t>(bd.usage) << "]";
    } else {
      oss << d.GetTypeInfo() << "['" << d.GetDebugName() << "']";
    }
    return oss.str();
  }
};

//! Factory function to create AsyncEngine-integrated validator
auto CreateAsyncEngineResourceValidator(GraphicsLayerIntegration* integration)
  -> std::unique_ptr<ResourceAliasValidator>
{
  return std::make_unique<AsyncEngineResourceAliasValidator>(integration);
}

} // namespace oxygen::examples::asyncsim
