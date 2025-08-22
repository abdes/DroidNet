//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "Types.h"

// Forward declarations for AsyncEngine integration
namespace oxygen::examples::asyncsim {
class GraphicsLayerIntegration;
}

namespace oxygen::examples::asyncsim {

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

  // Non-copyable, movable
  ResourceDesc(const ResourceDesc&) = delete;
  auto operator=(const ResourceDesc&) -> ResourceDesc& = delete;
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

  //! Set graphics layer integration for bindless resource management
  auto SetGraphicsIntegration(GraphicsLayerIntegration* integration) -> void
  {
    graphics_integration_ = integration;
  }

  //! Check if AsyncEngine integration is available
  [[nodiscard]] auto HasGraphicsIntegration() const -> bool
  {
    return graphics_integration_ != nullptr;
  }

  //! Get hash for resource compatibility checks
  [[nodiscard]] virtual auto GetCompatibilityHash() const -> std::size_t = 0;

protected:
  std::string debug_name_;
  ResourceLifetime lifetime_ { ResourceLifetime::FrameLocal };
  ResourceScope scope_ { ResourceScope::PerView };

  // AsyncEngine integration
  GraphicsLayerIntegration* graphics_integration_ { nullptr };
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
};

//! Resource aliasing hazard information
struct AliasHazard {
  ResourceHandle resource_a;
  ResourceHandle resource_b;
  std::vector<PassHandle> conflicting_passes;
  std::string description;
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
  [[nodiscard]] virtual auto ValidateAliasing() -> std::vector<AliasHazard>
  {
    // Stub implementation - Phase 1
    return {};
  }

  //! Check if two resource descriptors are compatible for aliasing
  [[nodiscard]] virtual auto AreCompatible(
    const ResourceDesc& a, const ResourceDesc& b) -> bool
  {
    // Enhanced compatibility check using type info and hash
    return a.GetTypeInfo() == b.GetTypeInfo()
      && a.GetCompatibilityHash() == b.GetCompatibilityHash();
  }

  //! Get debug information about resource aliasing
  [[nodiscard]] virtual auto GetDebugInfo() const -> std::string
  {
    return "ResourceAliasValidator (stub implementation)";
  }
};

} // namespace oxygen::examples::asyncsim
