//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <cstdint>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <unordered_map>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Graphics/Common/NativeObject.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/api_export.h>

namespace oxygen {
class Graphics;
} // namespace oxygen

namespace oxygen::graphics {
class Texture;
} // namespace oxygen::graphics

namespace oxygen::engine::upload {
class UploadCoordinator;
class StagingProvider;
} // namespace oxygen::engine::upload

namespace oxygen::engine::internal {

struct BrdfLutParams {
  uint32_t resolution { 0u };
  uint32_t sample_count { 0u };
};

inline constexpr BrdfLutParams kDefaultBrdfLutParams { 256u, 128u };

struct LutResult {
  std::shared_ptr<graphics::Texture> texture;
  ShaderVisibleIndex index { kInvalidShaderVisibleIndex };
};

class IBrdfLutProvider {
public:
  virtual ~IBrdfLutProvider() = default;

  [[nodiscard]] virtual auto GetOrCreateLut(
    BrdfLutParams params = kDefaultBrdfLutParams) -> LutResult
    = 0;
};

//! Generates and binds BRDF integration lookup tables (LUTs).
class BrdfLutManager : public IBrdfLutProvider {
public:
  using Params = BrdfLutParams;

  static constexpr Params kDefaultParams = kDefaultBrdfLutParams;

  OXGN_RNDR_API BrdfLutManager(observer_ptr<Graphics> gfx,
    observer_ptr<upload::UploadCoordinator> uploader,
    observer_ptr<upload::StagingProvider> staging_provider);

  OXGN_RNDR_API ~BrdfLutManager();

  OXYGEN_MAKE_NON_COPYABLE(BrdfLutManager)
  OXYGEN_DEFAULT_MOVABLE(BrdfLutManager)

  //! Returns a shader-visible index for a BRDF LUT, creating it if missing.
  [[nodiscard]] OXGN_RNDR_API auto GetOrCreateLut(
    Params params = kDefaultParams) -> LutResult override;

private:
  struct LutKey {
    uint32_t resolution { 0u };
    uint32_t sample_count { 0u };
    Format format { Format::kUnknown };

    auto operator==(const LutKey& other) const noexcept -> bool
    {
      return resolution == other.resolution
        && sample_count == other.sample_count && format == other.format;
    }
  };

  struct LutKeyHash {
    auto operator()(const LutKey& key) const noexcept -> std::size_t;
  };

  struct LutEntry {
    std::shared_ptr<graphics::Texture> texture;
    graphics::NativeView srv_view {};
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    std::optional<std::future<std::vector<std::byte>>> pending_generation {};
    std::optional<upload::UploadTicket> pending_ticket {};
  };

  observer_ptr<Graphics> gfx_;
  observer_ptr<upload::UploadCoordinator> uploader_;
  observer_ptr<upload::StagingProvider> staging_;

  std::unordered_map<LutKey, LutEntry, LutKeyHash> luts_;

  [[nodiscard]] auto EnsureLut(const LutKey& key) -> LutEntry*;
  [[nodiscard]] auto CreateTexture(const LutKey& key)
    -> std::shared_ptr<graphics::Texture>;
  [[nodiscard]] auto UploadTexture(const LutKey& key,
    graphics::Texture& texture) -> std::optional<upload::UploadTicket>;
  [[nodiscard]] auto UploadTexture(const LutKey& key, graphics::Texture& texture,
    std::span<const std::byte> data) -> std::optional<upload::UploadTicket>;

  struct SrvAllocation {
    graphics::NativeView view;
    ShaderVisibleIndex index { kInvalidShaderVisibleIndex };
  };

  [[nodiscard]] auto CreateSrv(
    const LutKey& key, const std::shared_ptr<graphics::Texture>& texture)
    -> std::optional<SrvAllocation>;

  [[nodiscard]] static auto GenerateLutData(const LutKey& key)
    -> std::vector<std::byte>;
};

} // namespace oxygen::engine::internal
