//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <iomanip>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Renderer/Resources/TextureBinder.h>
#include <Oxygen/Renderer/Upload/Errors.h>
#include <Oxygen/Renderer/Upload/StagingProvider.h>
#include <Oxygen/Renderer/Upload/Types.h>
#include <Oxygen/Renderer/Upload/UploadCoordinator.h>

namespace oxygen::renderer::resources {

namespace {

  // Limit how much CPU-visible staging memory TextureBinder can consume per
  // frame. This directly bounds RingBufferStaging growth (per partition) and
  // avoids multi-GB upload buffers when many large textures become ready at
  // once.
  constexpr std::size_t kMaxTextureUploadBytesPerFrame
    = 128ULL * 1024ULL * 1024ULL;

  struct UploadLayout {
    std::vector<engine::upload::UploadSubresource> dst_subresources;
    engine::upload::UploadTextureSourceView src_view;
    std::size_t trailing_bytes { 0U };
  };

  struct UploadLayoutFailure {
    enum class Reason : uint8_t {
      kLayoutCountMismatch,
      kSubresourceOutOfBounds,
      kRowPitchTooSmall,
      kSizeMismatch,
      kArithmeticOverflow,
    };

    Reason reason { Reason::kSubresourceOutOfBounds };
    std::uint32_t mip { 0U };
    std::uint32_t layer { 0U };
    std::size_t offset { 0U };
    std::size_t expected_value { 0U };
    std::size_t actual_value { 0U };
    std::size_t total_bytes { 0U };
  };

  [[nodiscard]] constexpr auto IsBc7Format(const Format format) noexcept -> bool
  {
    return format == Format::kBC7UNorm || format == Format::kBC7UNormSRGB;
  }

  [[nodiscard]] auto IsSupportedTextureFormat(const Format format,
    const graphics::detail::FormatInfo& info) noexcept -> bool
  {
    if (info.bytes_per_block == 0U || info.block_size == 0U) {
      return false;
    }
    // Engine only supports uncompressed formats and BC7.
    if (info.block_size > 1U) {
      return IsBc7Format(format);
    }
    return true;
  }

  [[nodiscard]] constexpr auto SafeAddSizeT(const std::size_t a,
    const std::size_t b) noexcept -> std::optional<std::size_t>
  {
    if (a > (std::numeric_limits<std::size_t>::max)() - b) {
      return std::nullopt;
    }
    return a + b;
  }

  [[nodiscard]] auto EstimateTextureBytes(const graphics::TextureDesc& desc,
    const graphics::detail::FormatInfo& fmt) -> std::optional<std::size_t>
  {
    if (desc.width == 0U || desc.height == 0U || desc.mip_levels == 0U
      || desc.array_size == 0U) {
      return std::size_t { 0U };
    }
    if (fmt.bytes_per_block == 0U || fmt.block_size == 0U) {
      return std::nullopt;
    }

    const std::size_t block = static_cast<std::size_t>(fmt.block_size);
    const std::size_t bpb = static_cast<std::size_t>(fmt.bytes_per_block);

    std::size_t total = 0U;
    for (std::uint32_t layer = 0U; layer < desc.array_size; ++layer) {
      (void)layer;
      for (std::uint32_t mip = 0U; mip < desc.mip_levels; ++mip) {
        const auto mip_w = (std::max)(desc.width >> mip, 1U);
        const auto mip_h = (std::max)(desc.height >> mip, 1U);

        const auto blocks_x
          = (static_cast<std::size_t>(mip_w) + block - 1U) / block;
        const auto blocks_y
          = (static_cast<std::size_t>(mip_h) + block - 1U) / block;

        if (blocks_x > (std::numeric_limits<std::size_t>::max)() / bpb) {
          return std::nullopt;
        }
        const auto row_bytes = blocks_x * bpb;
        if (blocks_y > 0U
          && row_bytes > (std::numeric_limits<std::size_t>::max)() / blocks_y) {
          return std::nullopt;
        }
        const auto mip_bytes = row_bytes * blocks_y;

        const auto next = SafeAddSizeT(total, mip_bytes);
        if (!next.has_value()) {
          return std::nullopt;
        }
        total = *next;
      }
    }

    return total;
  }

  [[nodiscard]] auto PrettyBytes(const std::size_t bytes) -> std::string
  {
    static constexpr std::array<const char*, 5> kUnits
      = { "B", "KiB", "MiB", "GiB", "TiB" };

    double value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= 1024.0 && unit + 1U < kUnits.size()) {
      value /= 1024.0;
      ++unit;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value << ' ' << kUnits[unit];
    return oss.str();
  }

  //! Build upload layout for 2D textures, 2D arrays, and cubemaps.
  /*!
    Uses the cooked payload's subresource layout table as the authoritative
    source of offsets and pitches.

    Subresource ordering MUST be layer-major (layer outer, mip inner) to match
    both the cooker and D3D12 subresource indexing.

    The produced UploadSubresource entries always represent full-subresource
    uploads (width/height == 0), which is required for BC formats where small
    mips are not multiples of the block size.
  */
  [[nodiscard]] auto BuildTexture2DUploadLayoutFromPayload(
    const graphics::TextureDesc& desc,
    const graphics::detail::FormatInfo& format_info,
    const std::span<const std::byte> data_bytes,
    const std::span<const data::pak::SubresourceLayout> layouts)
    -> std::variant<UploadLayout, UploadLayoutFailure>
  {
    UploadLayout layout;

    const std::uint32_t mip_count = desc.mip_levels;
    const std::uint32_t array_layers = desc.array_size;
    const std::size_t total_data_size = data_bytes.size();

    const std::size_t expected_subresources
      = static_cast<std::size_t>(mip_count)
      * static_cast<std::size_t>(array_layers);
    if (layouts.size() != expected_subresources) {
      return UploadLayoutFailure {
        .reason = UploadLayoutFailure::Reason::kLayoutCountMismatch,
        .mip = 0U,
        .layer = 0U,
        .offset = 0U,
        .expected_value = expected_subresources,
        .actual_value = layouts.size(),
        .total_bytes = total_data_size,
      };
    }

    layout.dst_subresources.reserve(expected_subresources);
    layout.src_view.subresources.reserve(expected_subresources);

    std::size_t max_end = 0U;

    for (std::uint32_t layer = 0; layer < array_layers; ++layer) {
      for (std::uint32_t mip = 0; mip < mip_count; ++mip) {
        const std::size_t idx
          = static_cast<std::size_t>(layer) * mip_count + mip;
        const auto& sr_layout = layouts[idx];

        const auto mip_w = (std::max)(desc.width >> mip, 1U);
        const auto mip_h = (std::max)(desc.height >> mip, 1U);

        const auto block = static_cast<std::size_t>(format_info.block_size);
        const auto bpb = static_cast<std::size_t>(format_info.bytes_per_block);
        if (block == 0U || bpb == 0U) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kArithmeticOverflow,
            .mip = mip,
            .layer = layer,
            .offset = 0U,
            .expected_value = 0U,
            .actual_value = 0U,
            .total_bytes = total_data_size,
          };
        }

        const auto blocks_x
          = (static_cast<std::size_t>(mip_w) + block - 1U) / block;
        const auto blocks_y
          = (static_cast<std::size_t>(mip_h) + block - 1U) / block;

        if (blocks_x > (std::numeric_limits<std::size_t>::max)() / bpb) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kArithmeticOverflow,
            .mip = mip,
            .layer = layer,
            .offset = 0U,
            .expected_value = 0U,
            .actual_value = 0U,
            .total_bytes = total_data_size,
          };
        }

        const auto min_row_bytes = blocks_x * bpb;
        const auto row_pitch
          = static_cast<std::size_t>(sr_layout.row_pitch_bytes);
        if (row_pitch < min_row_bytes) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kRowPitchTooSmall,
            .mip = mip,
            .layer = layer,
            .offset = static_cast<std::size_t>(sr_layout.offset_bytes),
            .expected_value = min_row_bytes,
            .actual_value = row_pitch,
            .total_bytes = total_data_size,
          };
        }

        if (blocks_y > 0U
          && row_pitch > (std::numeric_limits<std::size_t>::max)() / blocks_y) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kArithmeticOverflow,
            .mip = mip,
            .layer = layer,
            .offset = static_cast<std::size_t>(sr_layout.offset_bytes),
            .expected_value = 0U,
            .actual_value = 0U,
            .total_bytes = total_data_size,
          };
        }

        const auto expected_size = row_pitch * blocks_y;
        const auto size_bytes = static_cast<std::size_t>(sr_layout.size_bytes);
        if (size_bytes != expected_size) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kSizeMismatch,
            .mip = mip,
            .layer = layer,
            .offset = static_cast<std::size_t>(sr_layout.offset_bytes),
            .expected_value = expected_size,
            .actual_value = size_bytes,
            .total_bytes = total_data_size,
          };
        }

        const auto offset = static_cast<std::size_t>(sr_layout.offset_bytes);
        if (offset > total_data_size || size_bytes > total_data_size - offset) {
          return UploadLayoutFailure {
            .reason = UploadLayoutFailure::Reason::kSubresourceOutOfBounds,
            .mip = mip,
            .layer = layer,
            .offset = offset,
            .expected_value = size_bytes,
            .actual_value = total_data_size - offset,
            .total_bytes = total_data_size,
          };
        }

        layout.dst_subresources.push_back(engine::upload::UploadSubresource {
          .mip = mip,
          .array_slice = layer,
          .x = 0U,
          .y = 0U,
          .z = 0U,
          .width = 0U,
          .height = 0U,
          .depth = 1U,
        });

        layout.src_view.subresources.push_back(
          engine::upload::UploadTextureSourceSubresource {
            .bytes = data_bytes.subspan(offset, size_bytes),
            .row_pitch = sr_layout.row_pitch_bytes,
            .slice_pitch = sr_layout.size_bytes,
          });

        max_end = (std::max)(max_end, offset + size_bytes);
      }
    }

    layout.trailing_bytes
      = (max_end <= total_data_size) ? (total_data_size - max_end) : 0U;
    return layout;
  }

  struct PreparedTexture2DUpload {
    graphics::TextureDesc desc;
    std::shared_ptr<graphics::Texture> new_texture;
    UploadLayout layout;
  };

  struct PrepareTexture2DUploadFailure {
    enum class Reason : uint8_t {
      kUnsupportedTextureType,
      kUnsupportedFormat,
      kUnsupportedDepth,
      kCreateTextureException,
      kCreateTextureReturnedNull,
      kLayoutFailure,
    };

    Reason reason { Reason::kCreateTextureReturnedNull };
    std::optional<UploadLayoutFailure> layout_failure;
  };

  [[nodiscard]] auto PrepareTexture2DUpload(const Graphics& gfx,
    const data::TextureResource& tex_res, const content::ResourceKey key)
    -> std::variant<PreparedTexture2DUpload, PrepareTexture2DUploadFailure>
  {
    // Build GPU texture description.
    graphics::TextureDesc desc;
    desc.texture_type = tex_res.GetTextureType();
    switch (desc.texture_type) {
    case TextureType::kTexture2D:
    case TextureType::kTexture2DArray:
    case TextureType::kTextureCube:
      break;
    default:
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kUnsupportedTextureType,
      };
    }
    desc.format = tex_res.GetFormat();
    desc.width = tex_res.GetWidth();
    desc.height = tex_res.GetHeight();
    desc.depth = tex_res.GetDepth();
    desc.mip_levels = tex_res.GetMipCount();
    desc.array_size = tex_res.GetArrayLayers();
    desc.is_shader_resource = true;
    desc.debug_name = std::string("Texture(") + content::to_string(key) + ")";

    const auto& format_info = graphics::detail::GetFormatInfo(desc.format);
    if (!IsSupportedTextureFormat(desc.format, format_info)) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kUnsupportedFormat,
      };
    }

    if (desc.depth != 1U) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kUnsupportedDepth,
      };
    }

    if (desc.texture_type == TextureType::kTextureCube
      && desc.array_size != 6U) {
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kUnsupportedTextureType,
      };
    }

    std::shared_ptr<graphics::Texture> new_texture;
    try {
      new_texture = gfx.CreateTexture(desc);
    } catch (const std::exception& e) {
      LOG_F(ERROR, "CreateTexture threw during async load for {}: {}", key,
        e.what());
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kCreateTextureException,
      };
    }

    if (!new_texture) {
      LOG_F(ERROR, "CreateTexture returned null during async load for {}", key);
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kCreateTextureReturnedNull,
      };
    }

    const auto& data_span = tex_res.GetData();
    const auto data_bytes = std::as_bytes(data_span);

    const auto layout_result = BuildTexture2DUploadLayoutFromPayload(
      desc, format_info, data_bytes, tex_res.GetSubresourceLayouts());
    if (std::holds_alternative<UploadLayoutFailure>(layout_result)) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kLayoutFailure,
        .layout_failure = std::get<UploadLayoutFailure>(layout_result),
      };
    }

    return PreparedTexture2DUpload {
      .desc = desc,
      .new_texture = std::move(new_texture),
      .layout = std::get<UploadLayout>(layout_result),
    };
  }

  struct MipRange {
    std::uint32_t base_mip_level { 0U };
    std::uint32_t num_mip_levels { 1U };
  };

  struct ArrayRange {
    std::uint32_t base_array_slice { 0U };
    std::uint32_t num_array_slices { 1U };
  };

  [[nodiscard]] auto MakeTextureSrvViewDesc(
    const Format format, const MipRange mips, const ArrayRange slices)
    -> graphics::TextureViewDescription
  {
    graphics::TextureViewDescription view_desc;
    view_desc.view_type = graphics::ResourceViewType::kTexture_SRV;
    view_desc.visibility = graphics::DescriptorVisibility::kShaderVisible;
    view_desc.format = format;
    view_desc.sub_resources = {
      .base_mip_level = mips.base_mip_level,
      .num_mip_levels = mips.num_mip_levels,
      .base_array_slice = slices.base_array_slice,
      .num_array_slices = slices.num_array_slices,
    };
    return view_desc;
  }

  auto ReleaseTextureNextFrame(graphics::ResourceRegistry& registry,
    graphics::detail::DeferredReclaimer& reclaimer,
    std::shared_ptr<graphics::Texture>&& texture) -> void
  {
    if (!texture) {
      return;
    }
    registry.UnRegisterResource(*texture);
    reclaimer.RegisterDeferredRelease(std::move(texture));
  }

  //! Generate a magenta/black checkerboard pattern for an error texture.
  auto GenerateErrorTextureData(const Extent<uint32_t> extent,
    const std::uint32_t tile_size_px) -> std::vector<std::uint32_t>
  {
    CHECK_F(extent.width > 0 && extent.height > 0,
      "Invalid error texture dimensions");
    CHECK_F(tile_size_px > 0, "Invalid error texture tile size");

    const auto width = extent.width;
    const auto height = extent.height;

    std::vector<std::uint32_t> pixels;
    pixels.resize(
      static_cast<std::size_t>(width) * static_cast<std::size_t>(height));

    // Packed RGBA8 in little-endian memory is 0xAABBGGRR. This value produces
    // R=255, G=0, B=255, A=255.

    for (std::uint32_t y = 0; y < height; ++y) {
      for (std::uint32_t x = 0; x < width; ++x) {
        constexpr std::uint32_t kBlack = 0xFF000000U;
        constexpr std::uint32_t kMagenta = 0xFFFF00FFU;
        const bool is_magenta
          = ((x / tile_size_px) + (y / tile_size_px)) % 2 == 0;
        pixels.at((static_cast<std::size_t>(y) * width) + x)
          = is_magenta ? kMagenta : kBlack;
      }
    }

    return pixels;
  }

} // namespace

//=== TextureBinder Implementation ===========================================//

class TextureBinder::Impl {
public:
  Impl(observer_ptr<Graphics> gfx, observer_ptr<ProviderT> staging_provider,
    observer_ptr<CoordinatorT> uploader,
    observer_ptr<content::IAssetLoader> texture_loader);

  ~Impl();

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto OnFrameStart() -> void;
  auto OnFrameEnd() -> void;
  auto GetOrAllocate(const content::ResourceKey& resource_key)
    -> ShaderVisibleIndex;
  [[nodiscard]] auto TryGetMipLevels(
    const content::ResourceKey& resource_key) const noexcept
    -> std::optional<std::uint32_t>;
  [[nodiscard]] auto IsResourceReady(
    const content::ResourceKey& resource_key) const noexcept -> bool;
  [[nodiscard]] auto GetErrorTextureIndex() const -> ShaderVisibleIndex;
  auto DumpEstimatedTextureMemory(std::size_t top_n) const -> void;

private:
  enum class FailurePolicy : uint8_t {
    kBindErrorTexture,
    kKeepPlaceholderBound,
  };

  struct TextureEntry {
    bool is_placeholder { true };
    bool load_failed { false };
    bool evicted { false };
    std::uint64_t generation { 0U };
    std::uint64_t pending_generation { 0U };

    std::optional<engine::upload::UploadTicket> pending_ticket;
    std::optional<graphics::TextureViewDescription> pending_view_desc;

    std::shared_ptr<graphics::Texture> texture;
    std::shared_ptr<graphics::Texture> placeholder_texture;

    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    bindless::HeapIndex descriptor_index { kInvalidBindlessHeapIndex };
  };

  struct CallbackGate {
    std::mutex mutex;
    bool alive { true };
  };

  struct PendingUpload {
    content::ResourceKey key;
    std::shared_ptr<data::TextureResource> resource;
    std::uint64_t generation { 0U };
  };

  struct PendingEviction {
    content::ResourceKey key;
    content::EvictionReason reason { content::EvictionReason::kRefCountZero };
  };

  auto CreatePlaceholderTexture(std::optional<content::ResourceKey> for_key)
    -> std::shared_ptr<graphics::Texture>;
  auto CreateErrorTexture() -> std::shared_ptr<graphics::Texture>;
  auto InitiateAsyncLoad(content::ResourceKey resource_key, TextureEntry& entry)
    -> void;

  auto OnTextureResourceLoaded(content::ResourceKey resource_key,
    std::uint64_t generation, std::shared_ptr<data::TextureResource> tex_res)
    -> void;

  auto SubmitQueuedTextureUploads(std::size_t max_bytes) -> void;

  auto HandleLoadFailure(content::ResourceKey resource_key, TextureEntry& entry,
    FailurePolicy policy,
    std::shared_ptr<graphics::Texture>&& texture_to_release) -> void;

  auto ProcessEvictions() -> void;
  auto ReleaseEntryTexturesIfOwned(TextureEntry& entry) -> void;

  [[nodiscard]] auto TryRepointEntryToErrorTexture(
    content::ResourceKey resource_key, const TextureEntry& entry) const -> bool;

  auto ReleaseEntryPlaceholderIfOwned(TextureEntry& entry) -> void;

  auto FindEntryOrLog(content::ResourceKey resource_key) -> TextureEntry*;

  auto SubmitTextureUpload(content::ResourceKey resource_key,
    TextureEntry& entry, const graphics::TextureDesc& desc,
    std::shared_ptr<graphics::Texture>&& new_texture,
    std::vector<engine::upload::UploadSubresource>&& dst_subresources,
    engine::upload::UploadTextureSourceView&& src_view,
    std::size_t trailing_bytes) -> void;

  auto SubmitTextureData(const std::shared_ptr<graphics::Texture>& texture,
    std::span<const std::byte> data, const char* debug_name) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<engine::upload::UploadCoordinator> uploader_;
  observer_ptr<engine::upload::StagingProvider> staging_provider_;
  observer_ptr<content::IAssetLoader> texture_loader_;

  std::shared_ptr<CallbackGate> callback_gate_;

  std::unordered_map<content::ResourceKey, TextureEntry> texture_map_;

  std::mutex pending_uploads_mutex_;
  std::deque<PendingUpload> pending_uploads_;

  std::mutex eviction_mutex_;
  std::deque<PendingEviction> pending_evictions_;

  content::IAssetLoader::EvictionSubscription eviction_subscription_ {};

  // The singleton global placeholder and error textures.
  std::shared_ptr<graphics::Texture> placeholder_texture_;
  ShaderVisibleIndex placeholder_tex_svi_ { kInvalidShaderVisibleIndex };
  std::shared_ptr<graphics::Texture> error_texture_;
  ShaderVisibleIndex error_text_svi_ { kInvalidShaderVisibleIndex };

  // Telemetry stats
  std::uint64_t total_get_or_allocate_calls_ { 0U };
  std::uint64_t total_upload_submissions_ { 0U };
  std::uint64_t cache_hits_ { 0U };
  std::uint64_t load_failures_ { 0U };
};

TextureBinder::TextureBinder(observer_ptr<Graphics> gfx,
  observer_ptr<ProviderT> staging_provider, observer_ptr<CoordinatorT> uploader,
  observer_ptr<content::IAssetLoader> texture_loader)
  : impl_(
      std::make_unique<Impl>(gfx, staging_provider, uploader, texture_loader))
{
}

TextureBinder::~TextureBinder() = default;

auto TextureBinder::OnFrameStart() -> void { impl_->OnFrameStart(); }

auto TextureBinder::IsResourceReady(
  const content::ResourceKey& key) const noexcept -> bool
{
  return impl_->IsResourceReady(key);
}

auto TextureBinder::TryGetMipLevels(
  const content::ResourceKey& key) const noexcept
  -> std::optional<std::uint32_t>
{
  return impl_->TryGetMipLevels(key);
}

/*!
 TextureBinder frame-end hook.

 `OnFrameEnd()` is intentionally a no-op.

 TextureBinder drains upload completions and repoints descriptors during
 `OnFrameStart()`. Any GPU-safe destruction is handled by the graphics
 backend's `DeferredReclaimer` on `Graphics::BeginFrame()` when the frame slot
 cycles.
*/
auto TextureBinder::OnFrameEnd() -> void { impl_->OnFrameEnd(); }

auto TextureBinder::DumpEstimatedTextureMemory(const std::size_t top_n) const
  -> void
{
  impl_->DumpEstimatedTextureMemory(top_n);
}

// Index-based allocation has been removed. Use the ResourceKey-only API.

auto TextureBinder::GetOrAllocate(const content::ResourceKey& resource_key)
  -> ShaderVisibleIndex
{
  return impl_->GetOrAllocate(resource_key);
}

auto TextureBinder::Impl::IsResourceReady(
  const content::ResourceKey& resource_key) const noexcept -> bool
{
  if (resource_key.IsPlaceholder()) {
    return false;
  }

  const auto it = texture_map_.find(resource_key);
  if (it == texture_map_.end()) {
    // The fast-path placeholder binding does not create entries.
    return false;
  }

  const auto& entry = it->second;
  if (entry.load_failed) {
    return false;
  }
  if (entry.pending_ticket.has_value()) {
    return false;
  }
  return !entry.is_placeholder;
}

auto TextureBinder::Impl::TryGetMipLevels(
  const content::ResourceKey& resource_key) const noexcept
  -> std::optional<std::uint32_t>
{
  if (resource_key.IsFallback()) {
    if (placeholder_texture_) {
      return placeholder_texture_->GetDescriptor().mip_levels;
    }
    return std::nullopt;
  }

  const auto it = texture_map_.find(resource_key);
  if (it == texture_map_.end()) {
    // The fast-path placeholder binding does not create entries.
    return std::nullopt;
  }

  const auto& entry = it->second;
  if (!entry.texture) {
    return std::nullopt;
  }

  return entry.texture->GetDescriptor().mip_levels;
}

auto TextureBinder::Impl::GetOrAllocate(
  const content::ResourceKey& resource_key) -> ShaderVisibleIndex
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  ++total_get_or_allocate_calls_;

  // ResourceKey(0) is treated as a renderer-side fallback sentinel.
  // Never pass it to the AssetLoader (which expects valid, type-encoded keys).
  if (resource_key.IsFallback()) {
    // This is an extremely hot path in typical renderer usage.
    // Keep the trace available, but only at very high verbosity.
    DLOG_F(6, "TextureBinder GetOrAllocate: fallback sentinel -> placeholder");
    return placeholder_tex_svi_;
  }

  auto it = texture_map_.find(resource_key);
  if (it != texture_map_.end()) {
    ++cache_hits_;
    // Cache hits can be extremely frequent (per-frame, per-material).
    DLOG_F(6, "TextureBinder GetOrAllocate: cache hit -> srv_index {}",
      it->second.srv_index);
    if (it->second.evicted) {
      auto& entry = it->second;
      DLOG_F(4,
        "TextureBinder GetOrAllocate: evicted entry -> reloading resource {}",
        resource_key);
      entry.evicted = false;
      entry.load_failed = false;
      entry.is_placeholder = true;
      entry.pending_ticket.reset();
      entry.pending_view_desc.reset();
      entry.pending_generation = 0U;
      entry.texture = placeholder_texture_;
      entry.placeholder_texture = placeholder_texture_;
      InitiateAsyncLoad(resource_key, entry);
    }
    // Preserve per-resource stable indices. On failure, the descriptor is
    // repointed to the error texture, but the shader-visible handle remains
    // the entry's SRV index.
    return it->second.srv_index;
  }

  DLOG_SCOPE_F(4, "TextureBinder GetOrAllocate (allocate)");
  DLOG_F(4, "resource: {}", resource_key);

  TextureEntry entry;
  entry.texture = CreatePlaceholderTexture(resource_key);
  if (!entry.texture) {
    LOG_F(ERROR,
      "Failed to create per-entry placeholder texture for resource key: {}",
      resource_key);
    ++load_failures_;
    entry.load_failed = true;
    entry.is_placeholder = false;
    entry.texture = error_texture_;
    entry.srv_index = error_text_svi_;
    texture_map_.emplace(resource_key, std::move(entry));
    DLOG_F(3, "allocated: per-entry placeholder failed -> error texture");
    return error_text_svi_;
  }

  entry.placeholder_texture = entry.texture;

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  const auto view_desc = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});

  auto handle = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
    graphics::DescriptorVisibility::kShaderVisible);
  if (!handle.IsValid()) {
    LOG_F(ERROR, "Failed to allocate descriptor for resource key: {}",
      resource_key);
    ++load_failures_;

    // Release the per-entry placeholder immediately (it is not registered).
    entry.texture.reset();
    entry.placeholder_texture.reset();

    entry.load_failed = true;
    entry.is_placeholder = false;
    entry.texture = error_texture_;
    entry.srv_index = error_text_svi_;
    entry.descriptor_index = kInvalidBindlessHeapIndex;

    texture_map_.emplace(resource_key, std::move(entry));
    DLOG_F(
      3, "allocated: descriptor allocation failed -> cached error texture");
    return error_text_svi_;
  }

  entry.descriptor_index = handle.GetBindlessHandle();
  DLOG_F(4, "descriptor_index: {}", entry.descriptor_index);

  entry.srv_index
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(handle).get());

  registry.Register(entry.texture);
  registry.RegisterView(*entry.texture, std::move(handle), view_desc);

  // Insert before initiating the load to ensure completion callbacks can
  // always resolve the entry even if the load completes synchronously.
  const auto result_index = entry.srv_index;
  auto [insert_it, inserted]
    = texture_map_.emplace(resource_key, std::move(entry));
  DCHECK_F(inserted);

  // Initiate async load using the opaque ResourceKey.
  InitiateAsyncLoad(resource_key, insert_it->second);

  DLOG_F(4, "Allocated SRV index {} for resource key {}", result_index,
    resource_key);

  DLOG_F(4, "srv_index: {}", result_index);

  return result_index;
}

//=== TextureBinder::Impl Implementation =====================================//

TextureBinder::Impl::Impl(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> staging_provider,
  const observer_ptr<CoordinatorT> uploader,
  const observer_ptr<content::IAssetLoader> texture_loader)
  : gfx_(gfx)
  , uploader_(uploader)
  , staging_provider_(staging_provider)
  , texture_loader_(texture_loader)
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  CHECK_NOTNULL_F(texture_loader_, "IAssetLoader cannot be null");

  callback_gate_ = std::make_shared<CallbackGate>();
  CHECK_NOTNULL_F(callback_gate_, "Failed to create callback gate");

  eviction_subscription_ = texture_loader_->SubscribeResourceEvictions(
    data::TextureResource::ClassTypeId(),
    [gate = callback_gate_, this](const content::EvictionEvent& event) -> void {
      if (!gate) {
        return;
      }

      std::scoped_lock lock(gate->mutex);
      if (!gate->alive) {
        return;
      }

      if (event.reason == content::EvictionReason::kRefCountZero) {
        return;
      }

      LOG_F(2, "TextureBinder: eviction notification for {} (reason={})",
        event.key, event.reason);

      std::scoped_lock eviction_lock(eviction_mutex_);
      pending_evictions_.push_back(PendingEviction {
        .key = event.key,
        .reason = event.reason,
      });
    });

  error_texture_ = CreateErrorTexture();
  CHECK_NOTNULL_F(error_texture_, "Failed to create error texture");

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  const auto error_view_desc
    = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});

  auto error_handle
    = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
      graphics::DescriptorVisibility::kShaderVisible);
  CHECK_F(
    error_handle.IsValid(), "Failed to allocate error texture descriptor");

  error_text_svi_
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(error_handle).get());

  registry.Register(error_texture_);
  registry.RegisterView(
    *error_texture_, std::move(error_handle), error_view_desc);

  placeholder_texture_ = CreatePlaceholderTexture(std::nullopt);
  if (!placeholder_texture_) {
    LOG_F(ERROR,
      "Failed to create placeholder texture; using error texture instead");
    placeholder_texture_ = error_texture_;
    placeholder_tex_svi_ = error_text_svi_;
  } else {
    auto placeholder_handle
      = allocator.Allocate(graphics::ResourceViewType::kTexture_SRV,
        graphics::DescriptorVisibility::kShaderVisible);
    CHECK_F(placeholder_handle.IsValid(),
      "Failed to allocate placeholder texture descriptor");

    placeholder_tex_svi_ = ShaderVisibleIndex(
      allocator.GetShaderVisibleIndex(placeholder_handle).get());

    registry.Register(placeholder_texture_);
    registry.RegisterView(
      *placeholder_texture_, std::move(placeholder_handle), error_view_desc);
  }

  LOG_F(INFO, "TextureBinder initialized with error texture at SRV index: {}",
    error_text_svi_);
  LOG_F(INFO,
    "TextureBinder initialized with placeholder texture at SRV index: {}",
    placeholder_tex_svi_);
}

TextureBinder::Impl::~Impl()
{
  if (callback_gate_) {
    std::scoped_lock lock(callback_gate_->mutex);
    callback_gate_->alive = false;
  }

  LOG_SCOPE_F(INFO, "TextureBinder Statistics");
  LOG_F(INFO, "GetOrAllocate calls  : {}", total_get_or_allocate_calls_);
  LOG_F(INFO, "upload submissions   : {}", total_upload_submissions_);
  LOG_F(INFO, "cache hits     : {}", cache_hits_);
  LOG_F(INFO, "load failures  : {}", load_failures_);
  LOG_F(INFO, "textures loaded: {}", texture_map_.size());
}

auto TextureBinder::Impl::OnFrameStart() -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DLOG_SCOPE_F(5, "TextureBinder OnFrameStart");
  DLOG_F(6, "entries: {}", texture_map_.size());
  ProcessEvictions();
  // Drain completed upload tickets and perform SRV repointing on the render
  // thread. This keeps descriptor updates serialized with other render-thread
  // mutations and relies on UploadCoordinator as the authoritative source of
  // upload completion.
  if (!uploader_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();

  for (auto& [resource_key, entry] : texture_map_) {
    if (!entry.pending_ticket.has_value()) {
      continue;
    }

    if (entry.evicted || entry.pending_generation != entry.generation) {
      DLOG_F(4,
        "Discarding upload completion for {} due to eviction/generation",
        resource_key);
      if (entry.texture && entry.texture != placeholder_texture_
        && entry.texture != error_texture_) {
        ReleaseTextureNextFrame(registry, reclaimer, std::move(entry.texture));
      }
      entry.texture = placeholder_texture_;
      entry.pending_ticket.reset();
      entry.pending_view_desc.reset();
      entry.pending_generation = 0U;
      continue;
    }

    const auto ticket = *entry.pending_ticket;
    const auto maybe_result = uploader_->TryGetResult(ticket);
    if (!maybe_result.has_value()) {
      // Not completed yet
      continue;
    }

    DLOG_SCOPE_F(4, "Upload completion");
    DLOG_F(4, "resource: {}", resource_key);
    DLOG_F(4, "ticket: {}", ticket.id);
    DLOG_F(4, "descriptor_index: {}", entry.descriptor_index);
    DLOG_F(4, "is_placeholder: {}", entry.is_placeholder);
    DLOG_F(4, "load_failed: {}", entry.load_failed);

    DLOG_F(2, "Upload ticket {} completed for resource key {}", ticket.id,
      resource_key);

    const auto& result = *maybe_result;
    DLOG_F(4, "result.success: {}", result.success);
    if (!result.success) {
      // Upload failure: keep the placeholder bound.
      //
      // UploadTracker can report failures for immediate/producer failures or
      // explicit cancellation. In these cases we avoid repointing the
      // descriptor to the error texture and keep the placeholder active.
      LOG_F(WARNING,
        "Texture upload failed for resource entry (ticket={}): keeping "
        "placeholder",
        ticket.id);

      entry.load_failed = true;
      entry.is_placeholder = true;

      // Drop the newly-created destination texture (if any) and keep the
      // original placeholder texture active.
      if (entry.texture && entry.placeholder_texture
        && entry.texture != entry.placeholder_texture) {
        DLOG_F(4, "dropping newly created texture and restoring placeholder");
        ReleaseTextureNextFrame(registry, reclaimer, std::move(entry.texture));
        entry.texture = entry.placeholder_texture;
      }
    } else {
      // Successful upload: repoint the descriptor to the final texture
      entry.is_placeholder = false;
      entry.load_failed = false;

      if (entry.descriptor_index == kInvalidBindlessHeapIndex
        || !entry.pending_view_desc.has_value()) {
        entry.pending_ticket.reset();
        entry.pending_view_desc.reset();
        continue;
      }

      const bool updated = registry.UpdateView(
        *entry.texture, entry.descriptor_index, *entry.pending_view_desc);
      if (!updated) {
        LOG_F(ERROR,
          "Failed to update SRV view after upload completion (ticket={})",
          ticket.id);
        entry.pending_ticket.reset();
        entry.pending_view_desc.reset();
        continue;
      }

      LOG_F(INFO,
        "Repointed descriptor {} to final texture for resource {} (ticket={})",
        entry.descriptor_index, resource_key, ticket.id);

      if (entry.placeholder_texture
        && entry.placeholder_texture != entry.texture
        && entry.placeholder_texture != placeholder_texture_
        && entry.placeholder_texture != error_texture_) {
        DLOG_F(4, "releasing entry placeholder texture");
        ReleaseTextureNextFrame(
          registry, reclaimer, std::move(entry.placeholder_texture));
      }
    }

    // Clear pending ticket and view desc after handling
    entry.pending_ticket.reset();
    entry.pending_view_desc.reset();
    entry.pending_generation = 0U;
  }

  SubmitQueuedTextureUploads(kMaxTextureUploadBytesPerFrame);
}

auto TextureBinder::Impl::OnFrameEnd() -> void { }

auto TextureBinder::Impl::GetErrorTextureIndex() const -> ShaderVisibleIndex
{
  return error_text_svi_;
}

auto TextureBinder::Impl::DumpEstimatedTextureMemory(
  const std::size_t top_n) const -> void
{
  if (top_n == 0U) {
    return;
  }

  struct Record {
    content::ResourceKey key;
    graphics::TextureDesc desc;
    std::size_t bytes;
  };

  std::vector<Record> records;
  records.reserve(texture_map_.size());

  std::size_t total_bytes = 0U;
  std::size_t count = 0U;

  for (const auto& [key, entry] : texture_map_) {
    if (!entry.texture) {
      continue;
    }

    const auto& desc = entry.texture->GetDescriptor();
    const auto& fmt = graphics::detail::GetFormatInfo(desc.format);
    const auto bytes_opt = EstimateTextureBytes(desc, fmt);
    if (!bytes_opt.has_value()) {
      continue;
    }

    records.push_back(Record {
      .key = key,
      .desc = desc,
      .bytes = *bytes_opt,
    });

    const auto next = SafeAddSizeT(total_bytes, *bytes_opt);
    if (next.has_value()) {
      total_bytes = *next;
    } else {
      total_bytes = (std::numeric_limits<std::size_t>::max)();
    }
    ++count;
  }

  std::sort(records.begin(), records.end(),
    [](const Record& a, const Record& b) { return a.bytes > b.bytes; });

  const auto emit_count = (std::min)(records.size(), top_n);

  LOG_F(INFO,
    "TextureBinder: estimated GPU texture memory: total={} across {} textures "
    "(top {} shown)",
    PrettyBytes(total_bytes).c_str(), count, emit_count);

  for (std::size_t i = 0U; i < emit_count; ++i) {
    const auto& r = records[i];
    LOG_F(INFO, "  #{} {}: {} ({}, {}x{}x{}, mips={}, layers={})", i + 1U,
      r.key, PrettyBytes(r.bytes).c_str(), to_string(r.desc.format),
      r.desc.width, r.desc.height, r.desc.depth, r.desc.mip_levels,
      r.desc.array_size);
  }
}

//=== Private Implementation =================================================//

/*!
 Creates a 1×1 white placeholder texture for immediate use while actual
 texture loads asynchronously.

 @return Placeholder texture, or nullptr on failure
*/
auto TextureBinder::Impl::CreatePlaceholderTexture(
  const std::optional<content::ResourceKey> for_key)
  -> std::shared_ptr<graphics::Texture>
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  graphics::TextureDesc desc;
  desc.texture_type = TextureType::kTexture2D;
  desc.format = Format::kRGBA8UNorm;
  desc.width = 1;
  desc.height = 1;
  desc.depth = 1;
  desc.mip_levels = 1;
  desc.array_size = 1;
  desc.is_shader_resource = true;
  if (for_key.has_value()) {
    desc.debug_name
      = std::string("Placeholder(") + content::to_string(*for_key) + ")";
  } else {
    desc.debug_name = "FallbackTexture";
  }

  try {
    auto texture = gfx_->CreateTexture(desc);
    if (!texture) {
      LOG_F(ERROR, "CreateTexture returned null for placeholder");
      return nullptr;
    }

    constexpr std::array white_pixel_data { static_cast<std::byte>(0xFF),
      static_cast<std::byte>(0xFF), static_cast<std::byte>(0xFF),
      static_cast<std::byte>(0xFF) };
    SubmitTextureData(texture, white_pixel_data, "TextureBinder.Placeholder");

    return texture;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Exception creating placeholder texture: {}", e.what());
    return nullptr;
  }
}

/*!
 Creates a high-contrast magenta/black checkerboard error-indicator texture.

 @return Error texture, or nullptr on failure
*/
auto TextureBinder::Impl::CreateErrorTexture()
  -> std::shared_ptr<graphics::Texture>
{
  constexpr Extent<uint32_t> kTextureDimensions { .width = 256, .height = 256 };

  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  graphics::TextureDesc desc;
  desc.texture_type = TextureType::kTexture2D;
  desc.format = Format::kRGBA8UNorm;
  desc.width = kTextureDimensions.width;
  desc.height = kTextureDimensions.height;
  desc.depth = 1;
  desc.mip_levels = 1;
  desc.array_size = 1;
  desc.is_shader_resource = true;
  desc.debug_name = "ErrorTexture";

  try {
    auto texture = gfx_->CreateTexture(desc);
    if (!texture) {
      LOG_F(ERROR, "CreateTexture returned null for error texture");
      return nullptr;
    }

    constexpr std::uint32_t kTileSizePx = 32;
    const auto pixels
      = GenerateErrorTextureData(Extent(desc.width, desc.height), kTileSizePx);

    const std::span pixel_span = pixels;
    const std::span<const std::byte> pixel_bytes = std::as_bytes(pixel_span);
    SubmitTextureData(texture, pixel_bytes, "TextureBinder.ErrorTexture");

    return texture;
  } catch (const std::exception& e) {
    LOG_F(ERROR, "Exception creating error texture: {}", e.what());
    return nullptr;
  }
}

/*!
 Initiates asynchronous loading of texture resource and schedules replacement
 of placeholder with final texture.

 @param resource_key Opaque ResourceKey identifying the resource to load
 @param entry Texture entry to update when load completes
*/
auto TextureBinder::Impl::InitiateAsyncLoad(
  content::ResourceKey resource_key, TextureEntry& entry) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DLOG_SCOPE_F(3, "TextureBinder InitiateAsyncLoad");
  DLOG_F(3, "resource: {}", resource_key);
  LOG_F(INFO, "Initiating async load for resource key: {}", resource_key);

  const auto generation = entry.generation;

  texture_loader_->StartLoadTexture(resource_key,
    [gate = callback_gate_, this, resource_key, generation](
      // NOLINTNEXTLINE(*-unnecessary-value-param)
      std::shared_ptr<data::TextureResource> tex_res) -> void {
      if (!gate) {
        return;
      }

      {
        std::scoped_lock lock(gate->mutex);
        if (!gate->alive) {
          return;
        }
      }

      this->OnTextureResourceLoaded(
        resource_key, generation, std::move(tex_res));
    });
}

/*!
 Handle completion of an async texture load request.

 This method is invoked on the engine/render thread by `AssetLoader` and is
 allowed to mutate graphics resources and `texture_map_`.

 - `gfx_` must be valid
 - `uploader_` and `staging_provider_` must be available for upload

 Postconditions:

 - On `tex_res == nullptr`, the entry transitions to the error texture
   (and attempts to repoint the descriptor immediately if one exists).
 - On success, an upload is submitted and the entry is placed in
   "upload pending" state by setting `pending_ticket` and `pending_view_desc`.

 @param resource_key Opaque key for the entry being updated.
 @param generation Generation captured when the load request was issued.
 @param tex_res Loaded texture resource, or `nullptr` on load failure.
*/
auto TextureBinder::Impl::OnTextureResourceLoaded(
  const content::ResourceKey resource_key, const std::uint64_t generation,
  // NOLINTNEXTLINE(*-unnecessary-value-param) - moved from a lambda capture
  std::shared_ptr<data::TextureResource> tex_res) -> void
{
  // This callback may execute off the render thread. Do not touch render
  // thread-owned state here (e.g. texture_map_, SRV descriptors).
  {
    std::scoped_lock lock(pending_uploads_mutex_);
    pending_uploads_.push_back(PendingUpload {
      .key = resource_key,
      .resource = std::move(tex_res),
      .generation = generation,
    });
  }

  if (texture_loader_) {
    (void)texture_loader_->ReleaseResource(resource_key);
  }
}

auto TextureBinder::Impl::FindEntryOrLog(
  const content::ResourceKey resource_key) -> TextureEntry*
{
  auto it = texture_map_.find(resource_key);
  if (it == texture_map_.end()) {
    LOG_F(
      WARNING, "Async load completed but entry missing for {}", resource_key);
    return nullptr;
  }
  return &it->second;
}

auto TextureBinder::Impl::SubmitQueuedTextureUploads(
  const std::size_t max_bytes) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  std::size_t submitted_bytes = 0U;

  for (;;) {
    if (submitted_bytes >= max_bytes) {
      return;
    }

    PendingUpload pending;
    {
      std::scoped_lock lock(pending_uploads_mutex_);
      if (pending_uploads_.empty()) {
        return;
      }
      pending = std::move(pending_uploads_.front());
      pending_uploads_.pop_front();
    }

    TextureEntry* entry_ptr = this->FindEntryOrLog(pending.key);
    if (entry_ptr == nullptr) {
      continue;
    }
    auto& entry = *entry_ptr;

    if (entry.evicted || pending.generation != entry.generation) {
      DLOG_F(4,
        "Discarding pending upload for {} due to eviction/generation mismatch",
        pending.key);
      continue;
    }

    if (!pending.resource) {
      LOG_F(WARNING, "Async texture load returned null for resource {}",
        pending.key);
      this->HandleLoadFailure(
        pending.key, entry, FailurePolicy::kBindErrorTexture, nullptr);
      continue;
    }

    const auto data_bytes = pending.resource->GetDataSize();
    if (data_bytes > max_bytes && submitted_bytes == 0U) {
      LOG_F(WARNING,
        "TextureBinder: texture {} requires {} bytes; exceeds per-frame "
        "budget {}. Submitting anyway.",
        pending.key, data_bytes, max_bytes);
    } else if (submitted_bytes + data_bytes > max_bytes) {
      {
        std::scoped_lock lock(pending_uploads_mutex_);
        pending_uploads_.push_front(std::move(pending));
      }
      return;
    }

    DLOG_F(2, "format: {}", pending.resource->GetFormat());
    DLOG_F(2, "size: {}x{}x{}", pending.resource->GetWidth(),
      pending.resource->GetHeight(), pending.resource->GetDepth());
    DLOG_F(2, "mips: {}", pending.resource->GetMipCount());
    DLOG_F(2, "layers: {}", pending.resource->GetArrayLayers());
    DLOG_F(2, "data_alignment: {}", pending.resource->GetDataAlignment());
    DLOG_F(2, "data_bytes: {}", pending.resource->GetData().size());

    const auto prepared_result
      = PrepareTexture2DUpload(*gfx_, *pending.resource, pending.key);
    if (std::holds_alternative<PrepareTexture2DUploadFailure>(
          prepared_result)) {
      const auto& failure
        = std::get<PrepareTexture2DUploadFailure>(prepared_result);
      switch (failure.reason) {
      case PrepareTexture2DUploadFailure::Reason::kUnsupportedTextureType:
        LOG_F(ERROR,
          "TextureBinder async upload only supports 2D textures, 2D arrays, "
          "and cubemaps");
        break;
      case PrepareTexture2DUploadFailure::Reason::kUnsupportedFormat:
        LOG_F(ERROR,
          "TextureBinder upload only supports uncompressed and BC7 formats");
        break;
      case PrepareTexture2DUploadFailure::Reason::kUnsupportedDepth:
        LOG_F(ERROR, "TextureBinder async upload only supports 2D textures");
        break;
      case PrepareTexture2DUploadFailure::Reason::kCreateTextureException:
        LOG_F(ERROR, "CreateTexture threw during async load");
        break;
      case PrepareTexture2DUploadFailure::Reason::kCreateTextureReturnedNull:
        LOG_F(ERROR, "CreateTexture returned null during async load");
        break;
      case PrepareTexture2DUploadFailure::Reason::kLayoutFailure: {
        DCHECK_F(failure.layout_failure.has_value());
        const auto& lf = *failure.layout_failure;

        switch (lf.reason) {
        case UploadLayoutFailure::Reason::kLayoutCountMismatch:
          LOG_F(ERROR,
            "TextureResource layout count mismatch: expected {} layouts, got "
            "{}",
            lf.expected_value, lf.actual_value);
          break;
        case UploadLayoutFailure::Reason::kSubresourceOutOfBounds:
          LOG_F(ERROR,
            "TextureResource subresource out of bounds: mip {} layer {} offset "
            "{} size {} (available {})",
            lf.mip, lf.layer, lf.offset, lf.expected_value, lf.actual_value);
          break;
        case UploadLayoutFailure::Reason::kRowPitchTooSmall:
          LOG_F(ERROR,
            "TextureResource row pitch too small: mip {} layer {} offset {} "
            "need >= {} bytes, got {}",
            lf.mip, lf.layer, lf.offset, lf.expected_value, lf.actual_value);
          break;
        case UploadLayoutFailure::Reason::kSizeMismatch:
          LOG_F(ERROR,
            "TextureResource subresource size mismatch: mip {} layer {} offset "
            "{} expected {} bytes, got {}",
            lf.mip, lf.layer, lf.offset, lf.expected_value, lf.actual_value);
          break;
        case UploadLayoutFailure::Reason::kArithmeticOverflow:
          LOG_F(ERROR,
            "TextureResource upload layout arithmetic overflow: mip {} layer "
            "{}",
            lf.mip, lf.layer);
          break;
        }
        break;
      }
      }

      this->HandleLoadFailure(
        pending.key, entry, FailurePolicy::kBindErrorTexture, nullptr);
      continue;
    }

    PreparedTexture2DUpload prepared
      = std::get<PreparedTexture2DUpload>(prepared_result);
    this->SubmitTextureUpload(pending.key, entry, prepared.desc,
      std::move(prepared.new_texture),
      std::move(prepared.layout.dst_subresources),
      std::move(prepared.layout.src_view), prepared.layout.trailing_bytes);

    submitted_bytes += data_bytes;
  }
}

//=== Eviction handling =====================================================//

/*!
 Drain pending eviction requests and repoint evicted entries to the global
 placeholder texture.

 This must execute on the render thread. It releases any owned GPU textures
 for the entry and clears in-flight upload state so late completions cannot
 resurrect evicted resources.

 @note Evicted entries retain their stable SRV indices; the descriptor is
       repointed to the global placeholder.
*/
auto TextureBinder::Impl::ProcessEvictions() -> void
{
  std::deque<PendingEviction> evictions;
  {
    std::scoped_lock lock(eviction_mutex_);
    if (pending_evictions_.empty()) {
      return;
    }
    evictions.swap(pending_evictions_);
  }

  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  auto& registry = gfx_->GetResourceRegistry();

  for (const auto& eviction : evictions) {
    auto it = texture_map_.find(eviction.key);
    if (it == texture_map_.end()) {
      DLOG_F(4, "TextureBinder eviction: entry missing for {}", eviction.key);
      continue;
    }

    auto& entry = it->second;
    if (entry.evicted) {
      continue;
    }

    entry.evicted = true;
    ++entry.generation;
    entry.pending_generation = 0U;
    entry.pending_ticket.reset();
    entry.pending_view_desc.reset();

    auto old_texture = entry.texture;
    auto old_placeholder = entry.placeholder_texture;

    if (entry.descriptor_index != kInvalidBindlessHeapIndex
      && placeholder_texture_) {
      const auto view_desc
        = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});
      const bool updated = registry.UpdateView(
        *placeholder_texture_, entry.descriptor_index, view_desc);
      if (!updated) {
        LOG_F(ERROR,
          "TextureBinder eviction failed to repoint descriptor {} for {}",
          entry.descriptor_index, eviction.key);
      }
    }

    entry.texture = placeholder_texture_;
    entry.placeholder_texture = placeholder_texture_;
    entry.is_placeholder = true;
    entry.load_failed = false;

    if (old_texture || old_placeholder) {
      if (old_texture == old_placeholder) {
        old_placeholder.reset();
      }
      auto& reclaimer = gfx_->GetDeferredReclaimer();
      if (old_texture && old_texture != placeholder_texture_
        && old_texture != error_texture_) {
        ReleaseTextureNextFrame(registry, reclaimer, std::move(old_texture));
      }
      if (old_placeholder && old_placeholder != placeholder_texture_
        && old_placeholder != error_texture_) {
        ReleaseTextureNextFrame(
          registry, reclaimer, std::move(old_placeholder));
      }
    }

    LOG_F(2, "TextureBinder: eviction processed for {} (reason={})",
      eviction.key, eviction.reason);
  }
}

//! Release any non-shared textures owned by an entry.
auto TextureBinder::Impl::ReleaseEntryTexturesIfOwned(TextureEntry& entry)
  -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();

  auto texture = std::move(entry.texture);
  auto placeholder = std::move(entry.placeholder_texture);
  const bool same_texture = texture && placeholder && texture == placeholder;

  const auto release_if_owned = [&](std::shared_ptr<graphics::Texture>&& tex) {
    if (!tex) {
      return;
    }
    if (tex == placeholder_texture_ || tex == error_texture_) {
      return;
    }
    ReleaseTextureNextFrame(registry, reclaimer, std::move(tex));
  };

  release_if_owned(std::move(texture));
  if (!same_texture) {
    release_if_owned(std::move(placeholder));
  }
}

auto TextureBinder::Impl::SubmitTextureUpload(
  const content::ResourceKey resource_key, TextureEntry& entry,
  const graphics::TextureDesc& desc,
  std::shared_ptr<graphics::Texture>&& new_texture,
  std::vector<engine::upload::UploadSubresource>&& dst_subresources,
  engine::upload::UploadTextureSourceView&& src_view,
  const std::size_t trailing_bytes) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  DLOG_SCOPE_F(3, "TextureBinder SubmitTextureUpload");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "debug_name: {}", desc.debug_name);
  DLOG_F(3, "format: {}", desc.format);
  DLOG_F(3, "size: {}x{}x{}", desc.width, desc.height, desc.depth);
  DLOG_F(3, "mips: {}", desc.mip_levels);
  DLOG_F(3, "layers: {}", desc.array_size);
  DLOG_F(3, "subresources: {}", dst_subresources.size());
  DLOG_F(3, "trailing_bytes: {}", trailing_bytes);

  if (entry.evicted) {
    DLOG_F(4, "Discarding texture upload submission for evicted resource {}",
      resource_key);
    return;
  }

  if (trailing_bytes != 0U) {
    LOG_F(INFO, "TextureResource had {} trailing bytes after planned upload",
      trailing_bytes);
  }

  engine::upload::UploadRequest req {
    .kind = engine::upload::UploadKind::kTexture2D,
    .debug_name = desc.debug_name,
    .desc = engine::upload::UploadTextureDesc {
      .dst = new_texture,
      .width = desc.width,
      .height = desc.height,
      .depth = desc.depth,
      .format = desc.format,
    },
    .subresources = std::move(dst_subresources),
    .data = std::move(src_view),
  };

  const auto upload_result = uploader_->Submit(req, *staging_provider_);
  if (!upload_result) {
    const auto error_code
      = oxygen::engine::upload::make_error_code(upload_result.error());
    LOG_F(ERROR, "TextureBinder upload failed ({}): {}", desc.debug_name,
      error_code.message());

    // Upload submission failure: keep the placeholder bound.
    this->HandleLoadFailure(resource_key, entry,
      FailurePolicy::kKeepPlaceholderBound, std::move(new_texture));
    return;
  }

  ++total_upload_submissions_;

  DLOG_F(3, "ticket: {}", upload_result->id);

  // Register the created texture so the ResourceRegistry can manage it and
  // allow us to UpdateView later when upload completes.
  auto& registry = gfx_->GetResourceRegistry();
  registry.Register(new_texture);

  const auto view_desc = MakeTextureSrvViewDesc(desc.format,
    MipRange { .base_mip_level = 0U, .num_mip_levels = desc.mip_levels },
    ArrayRange { .base_array_slice = 0U, .num_array_slices = desc.array_size });

  // Store pending ticket + view desc for OnFrameStart() to observe; also
  // set the entry.texture now so UpdateView can target it when complete.
  entry.pending_ticket = *upload_result;
  entry.pending_generation = entry.generation;
  entry.pending_view_desc = view_desc;
  entry.texture = std::move(new_texture);
  entry.is_placeholder = true;
  entry.load_failed = false;
  DLOG_F(3, "InitiateAsyncLoad: submitted upload ticket {} for resource {}",
    entry.pending_ticket->id, resource_key);
}

/*!
 Apply a load or upload failure policy to an entry.

 This centralizes the two distinct failure policies currently in use:

 - `FailurePolicy::kBindErrorTexture`: set the entry's texture to the shared
   error texture and repoint the descriptor (if present).
 - `FailurePolicy::kKeepPlaceholderBound`: keep the placeholder active; used
   for cases where upload submission failed and no GPU work was recorded.

 @param resource_key Opaque key for the failing entry.
 @param entry Entry to update.
 @param policy Failure policy to apply.
 @param texture_to_release Optional newly-created texture that should be
   released via deferred reclamation.
*/
auto TextureBinder::Impl::HandleLoadFailure(
  const content::ResourceKey resource_key, TextureEntry& entry,
  const FailurePolicy policy,
  std::shared_ptr<graphics::Texture>&& texture_to_release) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");

  DLOG_SCOPE_F(3, "TextureBinder HandleLoadFailure");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "policy: {}",
    (policy == FailurePolicy::kBindErrorTexture) ? "BindErrorTexture"
                                                 : "KeepPlaceholderBound");
  DLOG_F(3, "descriptor_index: {}", entry.descriptor_index);
  DLOG_F(3, "is_placeholder: {}", entry.is_placeholder);
  DLOG_F(3, "load_failed: {}", entry.load_failed);
  DLOG_F(3, "releasing_new_texture: {}", static_cast<bool>(texture_to_release));

  ++load_failures_;
  entry.load_failed = true;

  if (texture_to_release) {
    auto& registry = gfx_->GetResourceRegistry();
    auto& reclaimer = gfx_->GetDeferredReclaimer();
    try {
      registry.UnRegisterResource(*texture_to_release);
    } catch (const std::exception&) {
      // Not registered (or already unregistered); safe to continue.
      (void)0;
    }
    reclaimer.RegisterDeferredRelease(std::move(texture_to_release));
  }

  if (policy == FailurePolicy::kKeepPlaceholderBound) {
    entry.is_placeholder = true;
    return;
  }

  entry.is_placeholder = false;
  entry.texture = error_texture_;

  // If we already own a descriptor index, repoint it immediately to the error
  // texture so the shader sees the error indicator without requiring further
  // UI interaction.
  if (entry.descriptor_index == kInvalidBindlessHeapIndex) {
    return;
  }

  if (!this->TryRepointEntryToErrorTexture(resource_key, entry)) {
    return;
  }

  this->ReleaseEntryPlaceholderIfOwned(entry);
}

/*!
 Attempt to repoint an existing bindless SRV descriptor to the error texture.

 @param resource_key Opaque key for logging/context.
 @param entry Entry containing the descriptor index to update.
 @return `true` if the view was updated, otherwise `false`.
*/
auto TextureBinder::Impl::TryRepointEntryToErrorTexture(
  const content::ResourceKey resource_key, const TextureEntry& entry) const
  -> bool
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  CHECK_NOTNULL_F(error_texture_, "Error texture must be initialized");

  DLOG_SCOPE_F(3, "TextureBinder RepointEntryToErrorTexture");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "descriptor_index: {}", entry.descriptor_index);

  auto& registry = gfx_->GetResourceRegistry();
  const auto view_desc = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});
  const bool updated
    = registry.UpdateView(*error_texture_, entry.descriptor_index, view_desc);
  if (!updated) {
    LOG_F(ERROR, "Failed to repoint descriptor to error texture for {}",
      resource_key);
    return false;
  }

  LOG_F(INFO, "Repointed descriptor {} to error texture for resource {}",
    entry.descriptor_index, resource_key);
  return true;
}

/*!
 Release a per-entry placeholder texture if it is owned by the entry.

 The global placeholder texture and the shared error texture are never
 released here.

 @param entry Entry whose placeholder may be released.
*/
auto TextureBinder::Impl::ReleaseEntryPlaceholderIfOwned(TextureEntry& entry)
  -> void
{
  if (!entry.placeholder_texture) {
    return;
  }
  if (entry.placeholder_texture == entry.texture) {
    return;
  }
  if (entry.placeholder_texture == placeholder_texture_) {
    return;
  }
  if (entry.placeholder_texture == error_texture_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();
  ReleaseTextureNextFrame(
    registry, reclaimer, std::move(entry.placeholder_texture));
}

auto TextureBinder::Impl::SubmitTextureData(
  const std::shared_ptr<graphics::Texture>& texture,
  const std::span<const std::byte> data, const char* debug_name) -> void
{
  if (!texture || !uploader_ || !staging_provider_ || data.empty()) {
    return;
  }

  const auto& desc = texture->GetDescriptor();
  const auto& format_info = graphics::detail::GetFormatInfo(desc.format);
  if (format_info.block_size != 1U || format_info.bytes_per_block == 0U) {
    LOG_F(ERROR, "TextureBinder upload only supports non-BC formats");
    return;
  }

  if (desc.depth != 1U) {
    LOG_F(ERROR, "TextureBinder upload only supports Texture2D");
    return;
  }

  const auto bytes_per_row = static_cast<std::uint32_t>(desc.width)
    * static_cast<std::uint32_t>(format_info.bytes_per_block);
  const auto expected_bytes = static_cast<std::size_t>(bytes_per_row)
    * static_cast<std::size_t>(desc.height);
  if (data.size() != expected_bytes) {
    LOG_F(ERROR, "TextureBinder upload expected {} bytes for {}x{}, got {}",
      expected_bytes, desc.width, desc.height, data.size());
    return;
  }

  engine::upload::UploadTextureSourceView src_view;
  src_view.subresources.push_back({
    .bytes = data,
    .row_pitch = bytes_per_row,
    .slice_pitch = bytes_per_row * desc.height,
  });

  engine::upload::UploadRequest req {
    .kind = engine::upload::UploadKind::kTexture2D,
    .debug_name = debug_name,
    .desc = engine::upload::UploadTextureDesc {
      .dst = texture,
      .width = desc.width,
      .height = desc.height,
      .depth = desc.depth,
      .format = desc.format,
    },
    .subresources = {},
    .data = std::move(src_view),
  };

  const auto result = uploader_->Submit(req, *staging_provider_);
  if (!result) {
    const auto error_code
      = oxygen::engine::upload::make_error_code(result.error());
    LOG_F(ERROR, "TextureBinder upload failed ({}): {}", debug_name,
      error_code.message());
  }
}

} // namespace oxygen::renderer::resources
