//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../../Types/ViewIndex.h"
#include "Types.h"

// Generic pair hasher for our local pair-key maps. We avoid specializing
// std::hash for our pair combinations in headers to prevent ordering
// / instantiation issues across translation units. Use PairHash as the
// unordered_map hasher where needed.
template <typename A, typename B> struct PairHash {
  auto operator()(const std::pair<A, B>& p) const noexcept -> std::size_t
  {
    const auto h1 = std::hash<A> {}(p.first);
    const auto h2 = std::hash<B> {}(p.second);
    return h1 ^ (h2 << 1);
  }
};

namespace oxygen::engine::asyncsim {

//! Resource state enum with all GPU resource states
enum class ResourceState : uint32_t {
  // Common states
  Undefined, //!< Resource state is undefined
  Common, //!< Common state for initial resource creation

  // Read states (can be combined)
  VertexAndIndexBuffer, //!< Vertex/index buffer for input assembly
  ConstantBuffer, //!< Constant buffer for shaders
  PixelShaderResource, //!< Texture/buffer read by pixel shader
  NonPixelShaderResource, //!< Texture/buffer read by non-pixel shaders
  AllShaderResource, //!< Texture/buffer read by any shader stage
  CopySource, //!< Source for copy operations

  // Write states (exclusive)
  RenderTarget, //!< Color render target output
  DepthWrite, //!< Depth buffer with write access
  DepthRead, //!< Depth buffer with read-only access
  UnorderedAccess, //!< Unordered access view for compute
  CopyDestination, //!< Destination for copy operations
  Present //!< Ready for presentation to display
};

//! Base class for resource descriptors
class ResourceDesc {
public:
  ResourceDesc() = default;
  virtual ~ResourceDesc() = default;

  // Allow copying (needed for descriptor duplication during build)
  ResourceDesc(const ResourceDesc&) = default;
  auto operator=(const ResourceDesc&) -> ResourceDesc& = default;
  ResourceDesc(ResourceDesc&&) = default;
  auto operator=(ResourceDesc&&) -> ResourceDesc& = default;

  //! Get debug name for this resource
  [[nodiscard]] auto GetDebugName() const -> const std::string&
  {
    return debug_name_;
  }

  //! Set debug name for this resource
  auto SetDebugName(std::string name) -> void { debug_name_ = std::move(name); }

  //! Get the resource lifetime
  [[nodiscard]] auto GetLifetime() const -> ResourceLifetime
  {
    return lifetime_;
  }

  //! Set the resource lifetime
  auto SetLifetime(ResourceLifetime lifetime) -> void { lifetime_ = lifetime; }

  //! Get the resource scope
  [[nodiscard]] auto GetScope() const -> ResourceScope { return scope_; }

  //! Set the resource scope
  auto SetScope(ResourceScope scope) -> void { scope_ = scope; }

  //! Get type information for this resource descriptor
  [[nodiscard]] virtual auto GetTypeInfo() const -> std::string = 0;

  // === ASYNCENGINE INTEGRATION ===

  //! Get hash for resource compatibility checks
  [[nodiscard]] virtual auto GetCompatibilityHash() const -> std::size_t = 0;

  //! Format compatibility check (default: exact type + hash match)
  [[nodiscard]] virtual auto IsFormatCompatibleWith(
    const ResourceDesc& other) const -> bool
  {
    return GetTypeInfo() == other.GetTypeInfo()
      && GetCompatibilityHash() == other.GetCompatibilityHash();
  }

  // === Bindless integration (AsyncEngine Phase 2) ===
  //! Store allocated descriptor index (bindless table). 0xFFFFFFFF = invalid
  auto SetDescriptorIndex(uint32_t index) noexcept
  {
    descriptor_index_ = index;
  }
  [[nodiscard]] auto GetDescriptorIndex() const noexcept -> uint32_t
  {
    return descriptor_index_;
  }
  [[nodiscard]] auto HasDescriptor() const noexcept -> bool
  {
    return descriptor_index_ != InvalidDescriptor;
  }
  static constexpr uint32_t InvalidDescriptor = 0xFFFFFFFFu;

protected:
  std::string debug_name_;
  ResourceLifetime lifetime_ { ResourceLifetime::FrameLocal };
  ResourceScope scope_ { ResourceScope::PerView };

  // AsyncEngine integration
  uint32_t descriptor_index_ { InvalidDescriptor }; // bindless descriptor slot
};

//! Texture resource descriptor
class TextureDesc : public ResourceDesc {
public:
  uint32_t width { 0 };
  uint32_t height { 0 };
  uint32_t depth { 1 };
  uint32_t mip_levels { 1 };
  uint32_t array_size { 1 };
  uint32_t sample_count { 1 };
  uint32_t sample_quality { 0 };

  // Placeholder format - in real implementation would use graphics::Format
  enum class Format : uint32_t {
    Unknown,
    RGBA8_UNorm,
    RGBA16_Float,
    RGBA32_Float,
    D32_Float,
    D24_UNorm_S8_UInt
  } format { Format::Unknown };

  // Placeholder usage flags
  enum class Usage : uint32_t {
    None = 0,
    RenderTarget = 1 << 0,
    DepthStencil = 1 << 1,
    ShaderResource = 1 << 2,
    UnorderedAccess = 1 << 3
  } usage { Usage::None };

  TextureDesc() = default;

  TextureDesc(uint32_t w, uint32_t h, Format fmt, Usage use)
    : width(w)
    , height(h)
    , format(fmt)
    , usage(use)
  {
  }

  [[nodiscard]] auto GetTypeInfo() const -> std::string override
  {
    return "TextureDesc";
  }

  //! Get hash for resource compatibility checks
  [[nodiscard]] auto GetCompatibilityHash() const -> std::size_t override
  {
    // Simple hash combining key properties for aliasing compatibility
    std::size_t hash = 0;
    hash
      ^= std::hash<uint32_t> {}(width) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint32_t> {}(height) + 0x9e3779b9 + (hash << 6)
      + (hash >> 2);
    hash ^= std::hash<uint32_t> {}(static_cast<uint32_t>(format)) + 0x9e3779b9
      + (hash << 6) + (hash >> 2);
    hash ^= std::hash<uint32_t> {}(static_cast<uint32_t>(usage)) + 0x9e3779b9
      + (hash << 6) + (hash >> 2);
    return hash;
  }

  [[nodiscard]] auto IsFormatCompatibleWith(const ResourceDesc& other) const
    -> bool override
  {
    if (other.GetTypeInfo() != GetTypeInfo())
      return false;
    const auto& o = static_cast<const TextureDesc&>(other);
    // Allow alias if same dimensions & format or if both formats are same size
    // class (e.g., RGBA8_UNorm vs D24 depth not allowed)
    if (width != o.width || height != o.height || depth != o.depth)
      return false;
    if (format == o.format)
      return true;
    // Simple size class heuristic
    auto sizeClass = [](Format f) {
      switch (f) {
      case Format::RGBA8_UNorm:
        return 4u;
      case Format::D32_Float:
        return 4u;
      case Format::D24_UNorm_S8_UInt:
        return 4u;
      case Format::RGBA16_Float:
        return 8u;
      case Format::RGBA32_Float:
        return 16u;
      default:
        return 0u;
      }
    };
    return sizeClass(format) != 0u && sizeClass(format) == sizeClass(o.format)
      && usage == o.usage;
  }
};

//! Buffer resource descriptor
class BufferDesc : public ResourceDesc {
public:
  uint64_t size_bytes { 0 };
  uint32_t stride { 0 }; //!< Stride for structured buffers, 0 for raw buffers

  // Placeholder usage flags
  enum class Usage : uint32_t {
    None = 0,
    VertexBuffer = 1 << 0,
    IndexBuffer = 1 << 1,
    ConstantBuffer = 1 << 2,
    StructuredBuffer = 1 << 3,
    UnorderedAccess = 1 << 4
  } usage { Usage::None };

  BufferDesc() = default;

  BufferDesc(uint64_t size, Usage use, uint32_t elem_stride = 0)
    : size_bytes(size)
    , stride(elem_stride)
    , usage(use)
  {
  }

  [[nodiscard]] auto GetTypeInfo() const -> std::string override
  {
    return "BufferDesc";
  }

  //! Get hash for resource compatibility checks
  [[nodiscard]] auto GetCompatibilityHash() const -> std::size_t override
  {
    // Simple hash combining key properties for aliasing compatibility
    std::size_t hash = 0;
    hash ^= std::hash<uint64_t> {}(size_bytes) + 0x9e3779b9 + (hash << 6)
      + (hash >> 2);
    hash ^= std::hash<uint32_t> {}(stride) + 0x9e3779b9 + (hash << 6)
      + (hash >> 2);
    hash ^= std::hash<uint32_t> {}(static_cast<uint32_t>(usage)) + 0x9e3779b9
      + (hash << 6) + (hash >> 2);
    return hash;
  }

  [[nodiscard]] auto IsFormatCompatibleWith(const ResourceDesc& other) const
    -> bool override
  {
    if (other.GetTypeInfo() != GetTypeInfo())
      return false;
    const auto& o = static_cast<const BufferDesc&>(other);
    // Same size or either is transient smaller (permit alias if target large
    // enough)
    if (size_bytes == o.size_bytes)
      return usage == o.usage;
    auto max_size = std::max(size_bytes, o.size_bytes);
    auto min_size = std::min(size_bytes, o.size_bytes);
    // Allow larger buffer aliasing smaller if usage flags are a superset.
    auto usage_bits = static_cast<uint32_t>(usage);
    auto other_bits = static_cast<uint32_t>(o.usage);
    bool usage_compatible = (usage_bits | other_bits) == usage_bits
      || (usage_bits | other_bits) == other_bits;
    return min_size * 2 <= max_size
      ? false
      : usage_compatible; // reject if size disparity >2x to avoid fragmentation
                          // issues
  }
};

//! Resource usage information for lifetime tracking
struct ResourceUsage {
  PassHandle pass; //!< Pass that uses this resource
  ResourceState state; //!< Required resource state for this usage
  bool is_write_access; //!< True if this usage writes to the resource
  ViewIndex view_index; //!< View index for per-view resources

  ResourceUsage(
    PassHandle p, ResourceState s, bool write, ViewIndex view = ViewIndex { 0 })
    : pass(p)
    , state(s)
    , is_write_access(write)
    , view_index(view)
  {
  }
};

//! Resource lifetime analysis result
struct ResourceLifetimeInfo {
  PassHandle first_usage; //!< First pass that uses this resource
  PassHandle last_usage; //!< Last pass that uses this resource
  std::vector<ResourceUsage> usages; //!< All usages throughout the frame
  std::vector<ResourceHandle> aliases; //!< Resources this can alias with
  size_t memory_requirement; //!< Memory requirement in bytes
  bool has_write_conflicts; //!< True if has overlapping write operations
  // Optional explicit ordering indices (populated when topological order is
  // supplied). UINT32_MAX indicates unset.
  uint32_t first_index { std::numeric_limits<uint32_t>::max() };
  uint32_t last_index { std::numeric_limits<uint32_t>::max() };

  ResourceLifetimeInfo() = default;

  //! Check if this resource's lifetime overlaps with another
  [[nodiscard]] auto OverlapsWith(const ResourceLifetimeInfo& other) const
    -> bool;

  //! Get debug string for this lifetime info
  [[nodiscard]] auto GetDebugString() const -> std::string;
};

//! Resource aliasing hazard information
struct AliasHazard {
  ResourceHandle resource_a;
  ResourceHandle resource_b;
  std::vector<PassHandle> conflicting_passes;
  std::string description;
  enum class Severity : uint8_t { Warning, Error } severity { Severity::Error };
};

//! Potential safe aliasing candidate (no detected hazards)
struct AliasCandidate {
  ResourceHandle resource_a;
  ResourceHandle resource_b;
  size_t combined_memory; //!< Max of individual requirements
  std::string description; //!< Brief rationale / compatibility summary
};

//! Interface for validating resource aliasing
/*!
 Resource aliasing enables memory-efficient rendering by reusing GPU memory
 for resources with non-overlapping lifetimes. However, it requires careful
 validation to prevent hazards.
 */
class ResourceAliasValidator {
public:
  ResourceAliasValidator() = default;
  virtual ~ResourceAliasValidator() = default;

  // Non-copyable, movable
  ResourceAliasValidator(const ResourceAliasValidator&) = delete;
  auto operator=(const ResourceAliasValidator&)
    -> ResourceAliasValidator& = delete;
  ResourceAliasValidator(ResourceAliasValidator&&) = default;
  auto operator=(ResourceAliasValidator&&) -> ResourceAliasValidator& = default;

  //! Add a resource for lifetime tracking
  virtual auto AddResource(ResourceHandle handle, const ResourceDesc& desc)
    -> void
    = 0;

  //! Add a resource usage for lifetime analysis
  virtual auto AddResourceUsage(ResourceHandle resource, PassHandle pass,
    ResourceState state, bool is_write, ViewIndex view_index = ViewIndex { 0 })
    -> void
    = 0;

  //! Analyze resource lifetimes and build aliasing information
  virtual auto AnalyzeLifetimes() -> void = 0;

  //! Provide a topological execution order mapping (pass -> linear index)
  //! to improve lifetime interval derivation. Optional: implementations may
  //! ignore if not provided.
  virtual auto SetTopologicalOrder(
    const std::unordered_map<PassHandle, uint32_t>& order) -> void
  {
    (void)order; // default no-op
  }

  //! Get lifetime information for a resource
  [[nodiscard]] virtual auto GetLifetimeInfo(ResourceHandle handle) const
    -> const ResourceLifetimeInfo* = 0;

  //! Validate aliasing configuration and return any hazards found
  /*!
   Performs hazard detection during compilation:
   - Shared vs Per-View Conflicts: A Shared resource output cannot alias with
     a PerView resource if any subsequent PerView pass reads the shared resource
   - Lifetime Overlap: Resources can only alias if their active lifetimes don't
   overlap
   - Format Compatibility: Aliased resources must have compatible formats and
   usage flags
   */
  [[nodiscard]] virtual auto ValidateAliasing() -> std::vector<AliasHazard> = 0;

  //! Retrieve safe alias candidates (call after AnalyzeLifetimes). Default
  //! empty.
  [[nodiscard]] virtual auto GetAliasCandidates() const
    -> std::vector<AliasCandidate>
  {
    return {};
  }

  //! Check if two resource descriptors are compatible for aliasing
  [[nodiscard]] virtual auto AreCompatible(
    const ResourceDesc& a, const ResourceDesc& b) const -> bool
  {
    if (a.GetLifetime() != b.GetLifetime())
      return false; // require same lifetime category for now
    return a.IsFormatCompatibleWith(b) && b.IsFormatCompatibleWith(a);
  }

  //! Get debug information about resource aliasing
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "ResourceAliasValidator (base implementation)";
  }
};

//! Resource state transition information
struct ResourceTransition {
  ResourceHandle resource;
  ResourceState from_state;
  ResourceState to_state;
  PassHandle pass;
  ViewIndex view_index;

  ResourceTransition(ResourceHandle res, ResourceState from, ResourceState to,
    PassHandle p, ViewIndex view = ViewIndex { 0 })
    : resource(res)
    , from_state(from)
    , to_state(to)
    , pass(p)
    , view_index(view)
  {
  }
};

//! Memory pool allocation for resource aliasing
struct MemoryAllocation {
  size_t offset; //!< Offset within the memory pool
  size_t size; //!< Size of this allocation
  ResourceHandle resource; //!< Resource using this allocation
  bool is_active; //!< True if currently in use

  MemoryAllocation(size_t off, size_t sz, ResourceHandle res)
    : offset(off)
    , size(sz)
    , resource(res)
    , is_active(true)
  {
  }
};

//! Memory pool for resource aliasing
class ResourceMemoryPool {
public:
  ResourceMemoryPool() = default;
  ~ResourceMemoryPool() = default;

  // Non-copyable, movable
  ResourceMemoryPool(const ResourceMemoryPool&) = delete;
  auto operator=(const ResourceMemoryPool&) -> ResourceMemoryPool& = delete;
  ResourceMemoryPool(ResourceMemoryPool&&) = default;
  auto operator=(ResourceMemoryPool&&) -> ResourceMemoryPool& = default;

  //! Allocate memory for a resource
  [[nodiscard]] auto Allocate(ResourceHandle resource, size_t size,
    size_t alignment) -> std::optional<MemoryAllocation>;

  //! Free memory allocation
  auto Free(ResourceHandle resource) -> void;

  //! Get total pool size
  [[nodiscard]] auto GetTotalSize() const -> size_t { return total_size_; }

  //! Get current usage
  [[nodiscard]] auto GetUsedSize() const -> size_t { return used_size_; }

  //! Get peak usage during this frame
  [[nodiscard]] auto GetPeakUsage() const -> size_t { return peak_usage_; }

  //! Reset peak usage tracking
  auto ResetPeakUsage() -> void { peak_usage_ = used_size_; }

  //! Get debug information
  [[nodiscard]] auto GetDebugInfo() const -> std::string;

private:
  std::vector<MemoryAllocation> allocations_;
  size_t total_size_ { 0 };
  size_t used_size_ { 0 };
  size_t peak_usage_ { 0 };

  //! Find best fit for allocation
  [[nodiscard]] auto FindBestFit(size_t size, size_t alignment)
    -> std::optional<size_t>;

  //! Merge adjacent free allocations
  auto CoalesceFreed() -> void;
};

//! Resource state tracker for managing GPU resource transitions
class ResourceStateTracker {
public:
  ResourceStateTracker() = default;
  ~ResourceStateTracker() = default;

  // Non-copyable, movable
  ResourceStateTracker(const ResourceStateTracker&) = delete;
  auto operator=(const ResourceStateTracker&) -> ResourceStateTracker& = delete;
  ResourceStateTracker(ResourceStateTracker&&) = default;
  auto operator=(ResourceStateTracker&&) -> ResourceStateTracker& = default;

  //! Set initial state for a resource
  auto SetInitialState(ResourceHandle resource, ResourceState state,
    ViewIndex view_index = ViewIndex { 0 }) -> void;

  //! Request state transition for a resource
  auto RequestTransition(ResourceHandle resource, ResourceState new_state,
    PassHandle pass, ViewIndex view_index = ViewIndex { 0 }) -> void;

  //! Get current state of a resource
  [[nodiscard]] auto GetCurrentState(
    ResourceHandle resource, ViewIndex view_index = ViewIndex { 0 }) const
    -> std::optional<ResourceState>;

  //! Get all planned transitions
  [[nodiscard]] auto GetPlannedTransitions() const
    -> const std::vector<ResourceTransition>&
  {
    return planned_transitions_;
  }

  //! Clear all state tracking
  auto Reset() -> void;

  //! Get debug information
  [[nodiscard]] auto GetDebugInfo() const -> std::string;

private:
  struct ResourceStateEntry {
    ResourceState current_state;
    PassHandle last_used_pass;
  };

  // Map from (resource_handle, view_index) to state
  std::unordered_map<std::pair<ResourceHandle, ViewIndex>, ResourceStateEntry,
    PairHash<ResourceHandle, ViewIndex>>
    resource_states_;
  std::vector<ResourceTransition> planned_transitions_;
};

} // namespace oxygen::engine::asyncsim

// No std::hash specializations here; use PairHash for local pair-key maps.
