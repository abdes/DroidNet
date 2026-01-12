//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "BrdfLutManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include <glm/geometric.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <fmt/format.h>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::engine::internal {

using namespace std::chrono_literals;

using graphics::TextureDesc;
using oxygen::TextureType;

namespace {

  constexpr float kEpsilon = 1e-4F;

  [[nodiscard]] auto RadicalInverseVdc(std::uint32_t bits) noexcept -> float
  {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
  }

  [[nodiscard]] auto Hammersley(std::uint32_t i, std::uint32_t n) -> glm::vec2
  {
    const float u = static_cast<float>(i) / static_cast<float>(n);
    return { u, RadicalInverseVdc(i) };
  }

  [[nodiscard]] auto ImportanceSampleGgx(const glm::vec2& xi, float roughness)
    -> glm::vec3
  {
    const float a = roughness * roughness;
    const float phi = 2.0F * glm::pi<float>() * xi.x;
    const float cos_theta
      = std::sqrt((1.0F - xi.y) / (1.0F + (a * a - 1.0F) * xi.y));
    const float sin_theta = std::sqrt((1.0F - cos_theta) * (1.0F + cos_theta));

    return { std::cos(phi) * sin_theta, std::sin(phi) * sin_theta, cos_theta };
  }

  [[nodiscard]] auto GeometrySchlickGgx(float n_dot_v, float a) -> float
  {
    const float k = (a + 1.0F) * (a + 1.0F) * 0.125F;
    return n_dot_v / (n_dot_v * (1.0F - k) + k);
  }

  [[nodiscard]] auto GeometrySmith(float n_dot_v, float n_dot_l, float a)
    -> float
  {
    const float ggx1 = GeometrySchlickGgx(n_dot_v, a);
    const float ggx2 = GeometrySchlickGgx(n_dot_l, a);
    return ggx1 * ggx2;
  }

  [[nodiscard]] auto PackHalf2x16(const glm::vec2& v) noexcept -> std::uint32_t
  {
    const auto h0 = glm::packHalf1x16(v.x);
    const auto h1 = glm::packHalf1x16(v.y);
    return (static_cast<std::uint32_t>(h1) << 16U)
      | static_cast<std::uint32_t>(h0);
  }

  [[nodiscard]] auto IntegrateBrdf(const float n_dot_v, const float roughness,
    const std::uint32_t sample_count) -> glm::vec2
  {
    glm::vec3 v { std::sqrt(std::max(1.0F - n_dot_v * n_dot_v, 0.0F)), 0.0F,
      n_dot_v };

    float a_term = 0.0F;
    float b_term = 0.0F;

    for (std::uint32_t i = 0; i < sample_count; ++i) {
      const glm::vec2 xi = Hammersley(i, sample_count);
      const glm::vec3 h = ImportanceSampleGgx(xi, roughness);
      const glm::vec3 l = glm::normalize(2.0F * glm::dot(v, h) * h - v);

      const float n_dot_l = std::max(l.z, 0.0F);
      const float n_dot_h = std::max(h.z, kEpsilon);
      const float v_dot_h = std::max(glm::dot(v, h), kEpsilon);

      if (n_dot_l <= 0.0F) {
        continue;
      }

      const float g = GeometrySmith(n_dot_v, n_dot_l, roughness * roughness);
      const float g_vis = (g * v_dot_h) / (n_dot_h * n_dot_v + kEpsilon);
      const float fc = std::pow(1.0F - glm::dot(v, l), 5.0F);

      a_term += (1.0F - fc) * g_vis;
      b_term += fc * g_vis;
    }

    const float inv_samples = 1.0F / static_cast<float>(sample_count);
    return { a_term * inv_samples, b_term * inv_samples };
  }

} // namespace

auto BrdfLutManager::LutKeyHash::operator()(const LutKey& key) const noexcept
  -> std::size_t
{
  std::size_t h = 0U;
  oxygen::HashCombine(h, key.resolution);
  oxygen::HashCombine(h, key.sample_count);
  oxygen::HashCombine(h, key.format);
  return h;
}

BrdfLutManager::BrdfLutManager(observer_ptr<Graphics> gfx,
  observer_ptr<upload::UploadCoordinator> uploader,
  observer_ptr<upload::StagingProvider> staging_provider)
  : gfx_(gfx)
  , uploader_(uploader)
  , staging_(staging_provider)
{
}

BrdfLutManager::~BrdfLutManager()
{
  if (!gfx_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  for (auto& [unused_key, entry] : luts_) {
    (void)unused_key;
    if (!entry.texture) {
      continue;
    }
    if (registry.Contains(*entry.texture)) {
      if (entry.srv_view.get().IsValid()) {
        registry.UnRegisterView(*entry.texture, entry.srv_view);
      }
      registry.UnRegisterResource(*entry.texture);
    }
    entry.srv_view = {};
    entry.srv_index = kInvalidShaderVisibleIndex;
  }
}

auto BrdfLutManager::GetOrCreateLut(const Params params) -> LutResult
{
  const LutKey key { params.resolution, params.sample_count,
    Format::kRG16Float };
  if (auto* entry = EnsureLut(key)) {
    if (entry->pending_generation.has_value()) {
      const auto status = entry->pending_generation->wait_for(0ms);
      if (status != std::future_status::ready) {
        return LutResult { nullptr,
          ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
      }

      const auto data = entry->pending_generation->get();
      entry->pending_generation.reset();

      if (!uploader_) {
        LOG_F(WARNING,
          "BRDF LUT generation completed but uploader is unavailable");
        return LutResult { nullptr,
          ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
      }

      const auto ticket = UploadTexture(key, *entry->texture,
        std::span<const std::byte>(data.data(), data.size()));
      if (!ticket.has_value()) {
        LOG_F(ERROR, "BRDF LUT upload submission failed");

        if (auto it = luts_.find(key); it != luts_.end()) {
          auto& registry = gfx_->GetResourceRegistry();
          if (it->second.texture && registry.Contains(*it->second.texture)) {
            if (it->second.srv_view.get().IsValid()) {
              registry.UnRegisterView(*it->second.texture, it->second.srv_view);
            }
            registry.UnRegisterResource(*it->second.texture);
          }
          luts_.erase(it);
        }

        return LutResult { nullptr,
          ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
      }

      entry->pending_ticket = ticket;
      return LutResult { nullptr,
        ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
    }

    if (entry->pending_ticket.has_value()) {
      if (!uploader_) {
        LOG_F(WARNING,
          "BRDF LUT upload ticket pending but uploader is unavailable");
        return LutResult { nullptr,
          ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
      }

      const auto result = uploader_->TryGetResult(*entry->pending_ticket);
      if (!result.has_value()) {
        LOG_F(WARNING, "BRDF LUT upload PENDING, ticket_id={}, fence={}",
          entry->pending_ticket->id.get(), entry->pending_ticket->fence.get());
        return LutResult { nullptr,
          ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
      }

      LOG_F(WARNING, "BRDF LUT upload COMPLETED, success={}", result->success);

      if (!result->success) {
        if (result->error.has_value()) {
          const std::error_code ec = result->error.value();
          LOG_F(ERROR, "BRDF LUT upload failed: [{}] {}", ec.category().name(),
            ec.message());
        } else {
          LOG_F(ERROR, "BRDF LUT upload failed with unknown error");
        }

        if (auto it = luts_.find(key); it != luts_.end()) {
          auto& registry = gfx_->GetResourceRegistry();
          if (it->second.texture && registry.Contains(*it->second.texture)) {
            if (it->second.srv_view.get().IsValid()) {
              registry.UnRegisterView(*it->second.texture, it->second.srv_view);
            }
            registry.UnRegisterResource(*it->second.texture);
          }
          luts_.erase(it);
        }

        return LutResult { nullptr,
          ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
      }

      entry->pending_ticket.reset();
      LOG_F(INFO, "BRDF LUT ready ({}x{}, samples={}, srv_index={})",
        key.resolution, key.resolution, key.sample_count,
        entry->srv_index.get());
    }

    return LutResult { entry->texture, entry->srv_index };
  }
  return LutResult { nullptr,
    ShaderVisibleIndex { kInvalidShaderVisibleIndex } };
}

auto BrdfLutManager::EnsureLut(const LutKey& key) -> LutEntry*
{
  if (auto it = luts_.find(key); it != luts_.end()) {
    return std::addressof(it->second);
  }

  if (!gfx_ || !uploader_ || !staging_) {
    LOG_F(ERROR, "BrdfLutManager missing dependencies");
    return nullptr;
  }

  auto texture = CreateTexture(key);
  if (!texture) {
    return nullptr;
  }

  gfx_->GetResourceRegistry().Register(texture);

  auto srv = CreateSrv(key, texture);
  if (!srv) {
    gfx_->GetResourceRegistry().UnRegisterResource(*texture);
    return nullptr;
  }

  LutEntry entry;
  entry.texture = std::move(texture);
  entry.srv_view = std::move(srv->view);
  entry.srv_index = srv->index;
  entry.pending_generation.emplace(std::async(std::launch::async, [key] {
    return GenerateLutData(key);
  }));

  auto [it, inserted] = luts_.emplace(key, std::move(entry));
  DCHECK_F(inserted, "Failed to insert BRDF LUT entry");
  return std::addressof(it->second);
}

auto BrdfLutManager::CreateTexture(const LutKey& key)
  -> std::shared_ptr<graphics::Texture>
{
  TextureDesc desc;
  desc.width = key.resolution;
  desc.height = key.resolution;
  desc.mip_levels = 1u;
  desc.sample_count = 1u;
  desc.format = key.format;
  desc.texture_type = TextureType::kTexture2D;
  desc.debug_name
    = fmt::format("BRDF_LUT_{}x{}", key.resolution, key.resolution);
  desc.is_shader_resource = true;
  desc.is_uav = false;
  desc.is_render_target = false;
  // Use kCommon initial state for copy queue compatibility.
  // Copy queues use implicit promotion from COMMON to COPY_DEST.
  desc.initial_state = graphics::ResourceStates::kCommon;

  auto texture = gfx_->CreateTexture(desc);
  if (!texture) {
    LOG_F(ERROR, "BrdfLutManager failed to create texture");
    return nullptr;
  }

  texture->SetName(desc.debug_name);
  return texture;
}

auto BrdfLutManager::UploadTexture(const LutKey& key,
  graphics::Texture& texture) -> std::optional<upload::UploadTicket>
{
  const auto data = GenerateLutData(key);
  return UploadTexture(
    key, texture, std::span<const std::byte>(data.data(), data.size()));
}

auto BrdfLutManager::UploadTexture(const LutKey& key, graphics::Texture& texture,
  const std::span<const std::byte> data) -> std::optional<upload::UploadTicket>
{
  if (data.empty()) {
    LOG_F(ERROR, "BRDF LUT generation failed (empty data)");
    return std::nullopt;
  }

  const auto expected_bytes = static_cast<std::size_t>(key.resolution)
    * static_cast<std::size_t>(key.resolution) * sizeof(std::uint32_t);
  if (data.size() != expected_bytes) {
    LOG_F(ERROR,
      "BRDF LUT data size mismatch (expected={}, got={})", expected_bytes,
      data.size());
    return std::nullopt;
  }

  const auto row_pitch
    = static_cast<std::uint32_t>(key.resolution * sizeof(std::uint32_t));
  const auto slice_pitch
    = static_cast<std::uint32_t>(row_pitch * key.resolution);

  upload::UploadTextureSourceView src_view;
  src_view.subresources.push_back(upload::UploadTextureSourceSubresource {
    .bytes = data,
    .row_pitch = row_pitch,
    .slice_pitch = slice_pitch,
  });

  upload::UploadRequest request {
    .kind = upload::UploadKind::kTexture2D,
    .priority = upload::Priority { 0 },
    .debug_name = fmt::format("BRDF_LUT_{}x{}", key.resolution,
      key.resolution),
    .desc = upload::UploadTextureDesc {
      .dst = texture.shared_from_this(),
      .width = key.resolution,
      .height = key.resolution,
      .depth = 1u,
      .format = key.format,
    },
    .subresources = std::vector<upload::UploadSubresource> {
      upload::UploadSubresource {
        .mip = 0u,
        .array_slice = 0u,
        .x = 0u,
        .y = 0u,
        .z = 0u,
        .width = key.resolution,
        .height = key.resolution,
        .depth = 1u,
      },
    },
    .data = src_view,
  };

  const auto ticket_exp = uploader_->Submit(request, *staging_);
  if (!ticket_exp.has_value()) {
    const std::error_code ec = ticket_exp.error();
    LOG_F(ERROR, "BRDF LUT upload submission failed: [{}] {}",
      ec.category().name(), ec.message());
    return std::nullopt;
  }

  return ticket_exp.value();
}

auto BrdfLutManager::CreateSrv(
  const LutKey& key, const std::shared_ptr<graphics::Texture>& texture)
  -> std::optional<SrvAllocation>
{
  auto& allocator = gfx_->GetDescriptorAllocator();
  auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "BRDF LUT descriptor allocation failed");
    return std::nullopt;
  }

  graphics::TextureViewDescription view_desc;
  view_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
  view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
  view_desc.format = key.format;
  view_desc.dimension = TextureType::kTexture2D;

  auto& registry = gfx_->GetResourceRegistry();
  DCHECK_F(registry.Contains(*texture),
    "BRDF LUT texture must be registered before creating SRV");

  const auto shader_index = allocator.GetShaderVisibleIndex(handle);
  auto native_view
    = registry.RegisterView(*texture, std::move(handle), view_desc);

  return SrvAllocation {
    .view = native_view,
    .index = shader_index,
  };
}

auto BrdfLutManager::GenerateLutData(const LutKey& key)
  -> std::vector<std::byte>
{
  if (key.resolution == 0U || key.sample_count == 0U) {
    return {};
  }

  const auto texel_count
    = static_cast<std::size_t>(key.resolution) * key.resolution;
  std::vector<std::byte> data(texel_count * sizeof(std::uint32_t));

  std::size_t offset = 0U;
  const float inv_resolution = 1.0F / static_cast<float>(key.resolution);

  for (std::uint32_t y = 0; y < key.resolution; ++y) {
    const float roughness = (static_cast<float>(y) + 0.5F) * inv_resolution;
    for (std::uint32_t x = 0; x < key.resolution; ++x) {
      const float n_dot_v = (static_cast<float>(x) + 0.5F) * inv_resolution;
      const glm::vec2 integrated
        = IntegrateBrdf(n_dot_v, roughness, key.sample_count);
      const std::uint32_t packed = PackHalf2x16(integrated);
      std::memcpy(data.data() + offset, &packed, sizeof(packed));
      offset += sizeof(packed);
    }
  }

  return data;
}

} // namespace oxygen::engine::internal
