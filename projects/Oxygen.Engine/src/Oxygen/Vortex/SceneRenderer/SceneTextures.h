//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string_view>
#include <type_traits>

#include <glm/vec2.hpp>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/NamedType.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Vortex/api_export.h>

namespace oxygen::vortex {

struct SceneTexturesConfig {
  glm::uvec2 extent { 0, 0 };
  bool enable_velocity { true };
  bool enable_custom_depth { false };
  std::uint32_t gbuffer_count { 4 };
  std::uint32_t msaa_sample_count { 1 };
};

enum class GBufferIndex : std::uint8_t {
  kNormal = 0,
  kMaterial = 1,
  kBaseColor = 2,
  kCustomData = 3,
  kShadowFactors = 4,
  kWorldTangent = 5,
  kCount = 6,
  kActiveCount = 4,
};

class SceneTextureSetupMode {
public:
  enum class Flag : std::uint32_t { // NOLINT(*-enum-size)
    kNone = 0,
    kSceneDepth = OXYGEN_FLAG(0),
    kPartialDepth = OXYGEN_FLAG(1),
    kSceneVelocity = OXYGEN_FLAG(2),
    kGBuffers = OXYGEN_FLAG(3),
    kSceneColor = OXYGEN_FLAG(4),
    kStencil = OXYGEN_FLAG(5),
    kCustomDepth = OXYGEN_FLAG(6),
  };

  using FlagsBase
    = oxygen::NamedType<std::uint32_t, struct SceneTextureSetupFlagsTag,
      oxygen::Comparable, oxygen::BitWiseAndable, oxygen::BitWiseOrable,
      oxygen::BitWiseXorable, oxygen::BitWiseInvertable, oxygen::Hashable>;

  class Flags : public FlagsBase {
    static_assert(
      std::is_same_v<typename FlagsBase::UnderlyingType, std::uint32_t>);

  public:
    constexpr Flags() noexcept
      : Flags(Flag::kNone)
    {
    }

    explicit constexpr Flags(const Flag value) noexcept
      : FlagsBase(static_cast<std::uint32_t>(value))
    {
    }

    explicit constexpr Flags(const std::uint32_t value) noexcept
      : FlagsBase(value)
    {
    }

    Flags(std::initializer_list<Flag> flags)
      : FlagsBase(static_cast<std::uint32_t>(Flag::kNone))
    {
      auto new_value = get();
      for (const auto flag : flags) {
        new_value |= static_cast<std::uint32_t>(flag);
      }
      *this = Flags { new_value };
    }

    Flags(const Flags&) = default;
    auto operator=(const Flags&) -> Flags& = default;
    Flags(Flags&&) = default;
    auto operator=(Flags&&) -> Flags& = default;
    ~Flags() noexcept = default;
  };

  constexpr void Set(const Flag flag) noexcept { flags_ |= Flags { flag }; }

  constexpr void Clear(const Flag flag) noexcept { flags_ &= ~Flags { flag }; }

  constexpr void Reset() noexcept { flags_ = Flags {}; }

  [[nodiscard]] constexpr auto IsSet(const Flag flag) const noexcept -> bool
  {
    return (flags_.get() & static_cast<std::uint32_t>(flag)) != 0;
  }

  [[nodiscard]] constexpr auto GetFlags() const noexcept -> std::uint32_t
  {
    return flags_.get();
  }

  [[nodiscard]] constexpr auto GetMask() const noexcept -> Flags
  {
    return flags_;
  }

  constexpr void SetFlags(const Flags flags) noexcept { flags_ |= flags; }

  constexpr void SetFlags(const Flag flag) noexcept { Set(flag); }

private:
  Flags flags_;
};

[[nodiscard]] constexpr auto operator|(const SceneTextureSetupMode::Flag lhs,
  const SceneTextureSetupMode::Flag rhs) noexcept
  -> SceneTextureSetupMode::Flags
{
  return SceneTextureSetupMode::Flags {
    (SceneTextureSetupMode::Flags { lhs }
      | SceneTextureSetupMode::Flags { rhs })
      .get(),
  };
}

[[nodiscard]] constexpr auto operator|(const SceneTextureSetupMode::Flags lhs,
  const SceneTextureSetupMode::Flag rhs) noexcept
  -> SceneTextureSetupMode::Flags
{
  return SceneTextureSetupMode::Flags {
    (lhs | SceneTextureSetupMode::Flags { rhs }).get(),
  };
}

struct SceneTextureBindings {
  static constexpr std::uint32_t kInvalidIndex
    = std::numeric_limits<std::uint32_t>::max();

  static constexpr auto MakeInvalidGBufferSrvs() -> std::array<std::uint32_t,
    static_cast<std::size_t>(GBufferIndex::kActiveCount)>
  {
    return { kInvalidIndex, kInvalidIndex, kInvalidIndex, kInvalidIndex };
  }

  std::uint32_t scene_color_srv { kInvalidIndex };
  std::uint32_t scene_depth_srv { kInvalidIndex };
  std::uint32_t partial_depth_srv { kInvalidIndex };
  std::uint32_t velocity_srv { kInvalidIndex };
  std::uint32_t stencil_srv { kInvalidIndex };
  std::uint32_t custom_depth_srv { kInvalidIndex };
  std::uint32_t custom_stencil_srv { kInvalidIndex };
  std::array<std::uint32_t,
    static_cast<std::size_t>(GBufferIndex::kActiveCount)>
    gbuffer_srvs { MakeInvalidGBufferSrvs() };
  std::uint32_t scene_color_uav { kInvalidIndex };
  std::uint32_t velocity_uav { kInvalidIndex };
  std::uint32_t valid_flags { 0 };

  void Invalidate() noexcept;
};

struct SceneTextureAspectView {
  enum class Aspect : std::uint8_t {
    kDepth = 0,
    kStencil = 1,
  };

  graphics::Texture* texture { nullptr };
  Aspect aspect { Aspect::kDepth };

  [[nodiscard]] auto IsValid() const noexcept -> bool
  {
    return texture != nullptr;
  }
};

struct SceneTextureExtractRef {
  graphics::Texture* texture { nullptr };
  bool valid { false };
};

struct SceneTextureExtracts {
  SceneTextureExtractRef resolved_scene_color {};
  SceneTextureExtractRef resolved_scene_depth {};
  SceneTextureExtractRef prev_scene_depth {};
  SceneTextureExtractRef prev_velocity {};

  void Reset() noexcept;
};

class SceneTextures {
public:
  OXGN_VRTX_API explicit SceneTextures(
    Graphics& gfx, const SceneTexturesConfig& config);
  OXGN_VRTX_API ~SceneTextures();

  SceneTextures(const SceneTextures&) = delete;
  auto operator=(const SceneTextures&) -> SceneTextures& = delete;
  SceneTextures(SceneTextures&&) = delete;
  auto operator=(SceneTextures&&) -> SceneTextures& = delete;

  [[nodiscard]] OXGN_VRTX_API auto GetSceneColor() const -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneDepth() const -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetPartialDepth() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneColorResource() const
    -> const std::shared_ptr<graphics::Texture>&;
  [[nodiscard]] OXGN_VRTX_API auto GetSceneDepthResource() const
    -> const std::shared_ptr<graphics::Texture>&;
  [[nodiscard]] OXGN_VRTX_API auto GetStencil() const -> SceneTextureAspectView;

  [[nodiscard]] OXGN_VRTX_API auto GetGBuffer(GBufferIndex index) const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferResource(GBufferIndex index) const
    -> const std::shared_ptr<graphics::Texture>&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferNormal() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferMaterial() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferBaseColor() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferCustomData() const
    -> graphics::Texture&;
  [[nodiscard]] OXGN_VRTX_API auto GetGBufferCount() const noexcept
    -> std::uint32_t;

  [[nodiscard]] OXGN_VRTX_API auto GetVelocity() const -> graphics::Texture*;
  [[nodiscard]] OXGN_VRTX_API auto GetVelocityResource() const
    -> const std::shared_ptr<graphics::Texture>&;
  [[nodiscard]] OXGN_VRTX_API auto GetCustomDepth() const -> graphics::Texture*;
  [[nodiscard]] OXGN_VRTX_API auto GetCustomStencil() const
    -> SceneTextureAspectView;

  OXGN_VRTX_API void Resize(glm::uvec2 new_extent);
  OXGN_VRTX_API void RebuildWithGBuffers();

  [[nodiscard]] OXGN_VRTX_API auto GetExtent() const noexcept -> glm::uvec2;
  [[nodiscard]] OXGN_VRTX_API auto GetConfig() const noexcept
    -> const SceneTexturesConfig&;

private:
  // Substrate constraint: Graphics::CreateTexture() returns shared_ptr.
  // SceneTextures is the sole logical owner of these resources; the shared_ptr
  // is an artifact of the graphics API, not an invitation for shared ownership.
  struct RegisteredTexture {
    std::shared_ptr<graphics::Texture> resource;
  };

  OXGN_VRTX_API static void ValidateConfig(const SceneTexturesConfig& config);
  OXGN_VRTX_API static void ValidateExtent(glm::uvec2 extent);
  OXGN_VRTX_API void AllocateTextures();
  OXGN_VRTX_API void ReleaseTextures() noexcept;
  OXGN_VRTX_API void RegisterTexture(RegisteredTexture& texture);
  OXGN_VRTX_API void UnregisterTexture(RegisteredTexture& texture) noexcept;
  OXGN_VRTX_NDAPI auto CreateTexture(std::string_view debug_name, Format format,
    bool shader_resource, bool render_target = false, bool uav = false) const
    -> std::shared_ptr<graphics::Texture>;
  OXGN_VRTX_NDAPI auto RequireTexture(const RegisteredTexture& texture,
    std::string_view name) const -> graphics::Texture&;

  Graphics& gfx_;
  SceneTexturesConfig config_;
  RegisteredTexture scene_color_ {};
  RegisteredTexture scene_depth_ {};
  RegisteredTexture partial_depth_ {};
  std::array<RegisteredTexture, static_cast<std::size_t>(GBufferIndex::kCount)>
    gbuffers_ {};
  RegisteredTexture velocity_ {};
  RegisteredTexture custom_depth_ {};
};

} // namespace oxygen::vortex
