//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <exception>
#include <iomanip>
#include <ios>
#include <limits>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Base/ScopeGuard.h>
#ifdef OXYGEN_WITH_TRACY
#  include <Oxygen/Profiling/CpuProfileScope.h>
#  include <Oxygen/Profiling/ProfileScope.h>
#endif
#include <Oxygen/Base/Macros.h>
#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Base/Types/Geometry.h>
#include <Oxygen/Content/EvictionEvents.h>
#include <Oxygen/Content/IAssetLoader.h>
#include <Oxygen/Content/ResourceKey.h>
#include <Oxygen/Core/Bindless/Generated.BindlessAbi.h>
#include <Oxygen/Core/Bindless/Types.h>
#include <Oxygen/Core/Detail/FormatUtils.h>
#include <Oxygen/Core/Types/Format.h>
#include <Oxygen/Core/Types/TextureType.h>
#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/TextureResource.h>
#include <Oxygen/Graphics/Common/DescriptorAllocator.h>
#include <Oxygen/Graphics/Common/Detail/DeferredReclaimer.h>
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Nexus/GenerationTracker.h>
#include <Oxygen/Vortex/Resources/CubeIlluminance.h>
#include <Oxygen/Vortex/Resources/TextureBinder.h>
#include <Oxygen/Vortex/Upload/Errors.h>
#include <Oxygen/Vortex/Upload/StagingProvider.h>
#include <Oxygen/Vortex/Upload/Types.h>
#include <Oxygen/Vortex/Upload/UploadCoordinator.h>
#include <Oxygen/Vortex/Upload/UploadTicket.h>

namespace oxygen::vortex::resources {

namespace {

  struct UploadLayout {
    std::vector<vortex::upload::UploadSubresource> dst_subresources;
    vortex::upload::UploadTextureSourceView src_view;
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

    const auto block = static_cast<std::size_t>(fmt.block_size);
    const auto bpb = static_cast<std::size_t>(fmt.bytes_per_block);

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
    constexpr double kBinaryUnitScale = 1024.0;
    static constexpr std::array<const char*, 5> kUnits
      = { "B", "KiB", "MiB", "GiB", "TiB" };

    auto value = static_cast<double>(bytes);
    std::size_t unit = 0U;
    while (value >= kBinaryUnitScale && unit + 1U < kUnits.size()) {
      value /= kBinaryUnitScale;
      ++unit;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value << ' '
        << kUnits.at(unit);
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
    const std::span<const data::pak::render::SubresourceLayout> layouts)
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
          = (static_cast<std::size_t>(layer) * mip_count) + mip;
        const auto& sr_layout = layouts.subspan(idx, 1U).front();

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

        layout.dst_subresources.push_back(vortex::upload::UploadSubresource {
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
          vortex::upload::UploadTextureSourceSubresource {
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
    constexpr std::uint32_t kCubeFaceCount = 6U;

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
        .layout_failure = std::nullopt,
      };
    }
    desc.format = tex_res.GetFormat();
    desc.width = tex_res.GetWidth();
    desc.height = tex_res.GetHeight();
    desc.depth = tex_res.GetDepth();
    desc.mip_levels = tex_res.GetMipCount();
    desc.array_size = tex_res.GetArrayLayers();
    desc.is_shader_resource = true;
    // UploadCoordinator hands completed texture uploads back in Common.
    // Declare the same creation state so a consumer on another queue can
    // establish tracking before its first shader-resource transition.
    desc.initial_state = graphics::ResourceStates::kCommon;
    desc.debug_name = std::string("Texture(") + content::to_string(key) + ")";

    const auto& format_info = graphics::detail::GetFormatInfo(desc.format);
    if (!IsSupportedTextureFormat(desc.format, format_info)) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kUnsupportedFormat,
        .layout_failure = std::nullopt,
      };
    }

    if (desc.depth != 1U) {
      return PrepareTexture2DUploadFailure {
        .reason = PrepareTexture2DUploadFailure::Reason::kUnsupportedDepth,
        .layout_failure = std::nullopt,
      };
    }

    if (desc.texture_type == TextureType::kTextureCube
      && desc.array_size != kCubeFaceCount) {
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kUnsupportedTextureType,
        .layout_failure = std::nullopt,
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
        .layout_failure = std::nullopt,
      };
    }

    if (!new_texture) {
      LOG_F(ERROR, "CreateTexture returned null during async load for {}", key);
      return PrepareTexture2DUploadFailure {
        .reason
        = PrepareTexture2DUploadFailure::Reason::kCreateTextureReturnedNull,
        .layout_failure = std::nullopt,
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
    auto release = reclaimer.PrepareDeferredAction(
      [retained = texture]() mutable { retained.reset(); });
    registry.UnRegisterResource(*texture);
    reclaimer.CommitDeferredAction(std::move(release));
    texture.reset();
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
    observer_ptr<content::IAssetLoader> texture_loader, UploadLimits limits);

  ~Impl();

  OXYGEN_MAKE_NON_COPYABLE(Impl)
  OXYGEN_MAKE_NON_MOVABLE(Impl)

  auto OnFrameStart() -> void;
  auto EnsureFrameResources() -> void;
  auto OnFrameEnd() -> void;
  auto GetOrAllocate(const content::ResourceKey& resource_key)
    -> ShaderVisibleIndex;
  [[nodiscard]] auto TryGetMipLevels(
    const content::ResourceKey& resource_key) const noexcept
    -> std::optional<std::uint32_t>;
  [[nodiscard]] auto IsResourceReady(
    const content::ResourceKey& resource_key) const noexcept -> bool;
  [[nodiscard]] auto TryGetCubeIlluminance(
    const content::ResourceKey& key) const noexcept -> std::optional<float>;
  auto AcquireReadyTexture(const content::ResourceKey& key)
    -> std::shared_ptr<const ReadyTexture>;
  [[nodiscard]] auto HasResourceFailed(
    const content::ResourceKey& key) const noexcept -> bool;
  [[nodiscard]] auto GetErrorTextureIndex() const -> ShaderVisibleIndex;
  auto DumpEstimatedTextureMemory(std::size_t top_n) const -> void;
  [[nodiscard]] auto GetPendingUploadCount() const noexcept -> std::size_t;
  [[nodiscard]] auto GetContentRevision(
    ShaderVisibleIndex descriptor) const noexcept -> std::uint64_t
  {
    const auto found = content_revisions_.find(descriptor);
    return found != content_revisions_.end() ? found->second : 0U;
  }
  [[nodiscard]] auto GetResidentContentRevision() const noexcept
    -> std::uint64_t
  {
    return resident_content_revision_;
  }
  [[nodiscard]] auto GetPendingUploadBytes() const noexcept -> std::size_t;
  [[nodiscard]] auto GetDeferredRetryCount() const noexcept -> std::size_t;
  [[nodiscard]] auto GetPendingUploadByteBudget() const noexcept -> std::size_t;

private:
  mutable std::unordered_map<ShaderVisibleIndex, std::uint64_t>
    content_revisions_;
  // Advances with every descriptor repoint, whatever texture it serves.
  mutable std::uint64_t resident_content_revision_ { 0U };

  auto BumpContentRevision(const ShaderVisibleIndex descriptor) const -> void
  {
    ++content_revisions_[descriptor];
    ++resident_content_revision_;
  }
  enum class FailurePolicy : uint8_t {
    kBindErrorTexture,
    kKeepPlaceholderBound,
  };

  struct TextureEntry {
    bool is_placeholder { true };
    bool load_failed { false };
    bool evicted { false };
    bool eviction_pending { false };
    std::weak_ptr<const ReadyTexture> resident_lease;
    //! Upward illuminance of a float cube, measured from its CPU payload.
    std::optional<float> cube_illuminance;

    std::optional<vortex::upload::UploadTicket> pending_ticket;
    std::optional<graphics::TextureViewDescription> pending_view_desc;
    VersionedBindlessHandle pending_handle;

    std::shared_ptr<graphics::Texture> texture;
    std::shared_ptr<graphics::Texture> placeholder_texture;

    content::EvictionReason last_eviction_reason {
      content::EvictionReason::kRefCountZero,
    };

    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
    VersionedBindlessHandle descriptor_handle;
  };

  struct CallbackGate {
    std::mutex mutex;
    bool alive { true };
  };

  struct PendingUpload {
    content::ResourceKey key;
    std::shared_ptr<data::TextureResource> resource;
    VersionedBindlessHandle handle;
  };

  struct PendingCompletion {
    content::ResourceKey key;
    VersionedBindlessHandle handle;
    vortex::upload::TicketId ticket_id { 0 };
  };

  struct DeferredRetry {
    content::ResourceKey key;
    VersionedBindlessHandle handle;
  };

  struct PendingEviction {
    content::ResourceKey key;
    content::EvictionReason reason { content::EvictionReason::kRefCountZero };
    VersionedBindlessHandle handle;
  };

  auto CreatePlaceholderTexture(std::optional<content::ResourceKey> for_key)
    -> std::shared_ptr<graphics::Texture>;
  auto CreateErrorTexture() -> std::shared_ptr<graphics::Texture>;
  auto InitiateAsyncLoad(content::ResourceKey resource_key, TextureEntry& entry,
    const char* reason) -> void;

  auto OnTextureResourceLoaded(content::ResourceKey resource_key,
    VersionedBindlessHandle handle,
    std::shared_ptr<data::TextureResource> tex_res) -> void;

  auto DiscardStaleQueuedUploads() -> void;
  auto SubmitQueuedTextureUploads(std::size_t max_bytes) -> void;
  auto ReissueDeferredLoads(std::size_t max_retries) -> void;

  auto HandleLoadFailure(content::ResourceKey resource_key, TextureEntry& entry,
    FailurePolicy policy,
    std::shared_ptr<graphics::Texture>&& texture_to_release) -> void;

  auto AcceptEvictions() -> void;
  auto ProcessEvictions() -> void;
  auto PublishCompletedUploads() -> void;

  [[nodiscard]] auto TryRepointEntryToErrorTexture(
    content::ResourceKey resource_key, const TextureEntry& entry) const -> bool;

  auto ReleaseEntryPlaceholderIfOwned(TextureEntry& entry) -> void;

  auto FindEntryOrLog(content::ResourceKey resource_key) -> TextureEntry*;

  auto SubmitTextureUpload(content::ResourceKey resource_key,
    TextureEntry& entry, const graphics::TextureDesc& desc,
    std::shared_ptr<graphics::Texture>&& new_texture,
    std::vector<vortex::upload::UploadSubresource>&& dst_subresources,
    vortex::upload::UploadTextureSourceView&& src_view,
    std::size_t trailing_bytes) -> void;
  auto EnsureGenerationCapacity_(bindless::HeapIndex descriptor_index) -> void;
  [[nodiscard]] auto CurrentGeneration_(const TextureEntry& entry) const
    -> bindless::Generation;
  [[nodiscard]] auto CurrentDescriptorHandle_(const TextureEntry& entry) const
    -> VersionedBindlessHandle;

  auto SubmitTextureData(const std::shared_ptr<graphics::Texture>& texture,
    std::span<const std::byte> data, const char* debug_name) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<vortex::upload::UploadCoordinator> uploader_;
  observer_ptr<vortex::upload::StagingProvider> staging_provider_;
  observer_ptr<content::IAssetLoader> texture_loader_;
  UploadLimits limits_;
  bool frame_resources_ensured_ { false };
  std::list<PendingCompletion> pending_completions_;

  std::shared_ptr<CallbackGate> callback_gate_;

  std::unordered_map<content::ResourceKey, TextureEntry> texture_map_;
  nexus::GenerationTracker slot_generations_;
  std::uint32_t slot_generation_capacity_ { 0U };

  mutable std::mutex pending_uploads_mutex_;
  std::list<PendingUpload> pending_uploads_;
  std::list<PendingUpload>::iterator decoded_cleanup_cursor_ {
    pending_uploads_.end()
  };
  std::size_t decoded_cleanup_remaining_ { 0U };
  std::size_t pending_upload_bytes_ { 0U };

  mutable std::mutex deferred_retries_mutex_;
  std::deque<DeferredRetry> deferred_retries_;
  std::unordered_set<content::ResourceKey> deferred_retry_keys_;

  std::mutex eviction_mutex_;
  std::list<PendingEviction> pending_evictions_;
  std::atomic_bool has_pending_evictions_ { false };
  std::list<PendingEviction> eviction_work_;

  content::IAssetLoader::EvictionSubscription eviction_subscription_;

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
  std::uint64_t lifecycle_generation_bumps_ { 0U };
  std::uint64_t lifecycle_stale_discard_count_ { 0U };
  std::uint64_t lifecycle_async_enqueued_ { 0U };
  std::uint64_t lifecycle_async_backlog_deferrals_ { 0U };
  std::uint64_t lifecycle_async_retries_ { 0U };
};

TextureBinder::TextureBinder(observer_ptr<Graphics> gfx,
  observer_ptr<ProviderT> staging_provider, observer_ptr<CoordinatorT> uploader,
  observer_ptr<content::IAssetLoader> texture_loader, UploadLimits limits)
  : impl_(std::make_unique<Impl>(
      gfx, staging_provider, uploader, texture_loader, limits))
{
}

TextureBinder::TextureBinder(observer_ptr<Graphics> gfx,
  observer_ptr<ProviderT> staging_provider, observer_ptr<CoordinatorT> uploader,
  observer_ptr<content::IAssetLoader> texture_loader)
  : TextureBinder(gfx, staging_provider, uploader, texture_loader,
      TextureBinder::UploadLimits {})
{
}

TextureBinder::~TextureBinder() = default;

auto TextureBinder::OnFrameStart() -> void { impl_->OnFrameStart(); }
auto TextureBinder::EnsureFrameResources() -> void
{
  impl_->EnsureFrameResources();
}

auto TextureBinder::IsResourceReady(
  const content::ResourceKey& key) const noexcept -> bool
{
  return impl_->IsResourceReady(key);
}

auto TextureBinder::AcquireReadyTexture(const content::ResourceKey& key)
  -> std::shared_ptr<const ReadyTexture>
{
  return impl_->AcquireReadyTexture(key);
}

auto TextureBinder::HasResourceFailed(
  const content::ResourceKey& key) const noexcept -> bool
{
  return impl_->HasResourceFailed(key);
}

auto TextureBinder::TryGetCubeIlluminance(
  const content::ResourceKey& key) const noexcept -> std::optional<float>
{
  return impl_->TryGetCubeIlluminance(key);
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

auto TextureBinder::GetPendingUploadCount() const noexcept -> std::size_t
{
  return impl_->GetPendingUploadCount();
}

auto TextureBinder::GetResidentContentRevision() const noexcept -> std::uint64_t
{
  return impl_->GetResidentContentRevision();
}

auto TextureBinder::GetContentRevision(
  ShaderVisibleIndex descriptor) const noexcept -> std::uint64_t
{
  return impl_->GetContentRevision(descriptor);
}

auto TextureBinder::GetPendingUploadBytes() const noexcept -> std::size_t
{
  return impl_->GetPendingUploadBytes();
}

auto TextureBinder::GetDeferredRetryCount() const noexcept -> std::size_t
{
  return impl_->GetDeferredRetryCount();
}

auto TextureBinder::GetPendingUploadByteBudget() const noexcept -> std::size_t
{
  return impl_->GetPendingUploadByteBudget();
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
  if (resource_key.IsPlaceholder() || resource_key.IsError()) {
    return false;
  }

  const auto it = texture_map_.find(resource_key);
  if (it == texture_map_.end()) {
    // The fast-path placeholder binding does not create entries.
    return false;
  }

  const auto& entry = it->second;
  if (entry.load_failed
    || (entry.eviction_pending && entry.resident_lease.expired())) {
    return false;
  }
  if (entry.pending_ticket.has_value()) {
    return false;
  }
  return !entry.is_placeholder;
}

auto TextureBinder::Impl::TryGetCubeIlluminance(
  const content::ResourceKey& key) const noexcept -> std::optional<float>
{
  if (!IsResourceReady(key)) {
    return std::nullopt;
  }
  return texture_map_.at(key).cube_illuminance;
}

auto TextureBinder::Impl::HasResourceFailed(
  const content::ResourceKey& key) const noexcept -> bool
{
  if (key.IsError()) {
    return true;
  }
  const auto it = texture_map_.find(key);
  return it != texture_map_.end() && it->second.load_failed;
}

auto TextureBinder::Impl::AcquireReadyTexture(const content::ResourceKey& key)
  -> std::shared_ptr<const ReadyTexture>
{
  if (!IsResourceReady(key)) {
    return {};
  }
  auto& entry = texture_map_.at(key);
  if (auto lease = entry.resident_lease.lock()) {
    return lease;
  }
  auto lease = std::make_shared<const ReadyTexture>(ReadyTexture {
    .texture = entry.texture,
    .srv = entry.srv_index,
  });
  entry.resident_lease = lease;
  return lease;
}

auto TextureBinder::Impl::TryGetMipLevels(
  const content::ResourceKey& resource_key) const noexcept
  -> std::optional<std::uint32_t>
{
  if (resource_key.IsError()) {
    return error_texture_
      ? std::optional { error_texture_->GetDescriptor().mip_levels }
      : std::nullopt;
  }
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
    DLOG_F(6, "GetOrAllocate: fallback sentinel -> placeholder");
    return placeholder_tex_svi_;
  }
  if (resource_key.IsError()) {
    return error_text_svi_;
  }

  AcceptEvictions();
  auto it = texture_map_.find(resource_key);
  if (it != texture_map_.end()) {
    ++cache_hits_;
    // Cache hits can be extremely frequent (per-frame, per-material).
    DLOG_F(6, "GetOrAllocate: cache hit -> srv_index {}", it->second.srv_index);
    if (it->second.evicted) {
      auto& entry = it->second;
      DLOG_F(4, "GetOrAllocate: evicted entry -> reloading resource {}",
        resource_key);
      LOG_F(INFO, "Reloading evicted resource {} (reason={}, gen={})",
        resource_key, entry.last_eviction_reason,
        CurrentGeneration_(entry).get());
      entry.evicted = false;
      entry.load_failed = false;
      entry.is_placeholder = true;
      entry.pending_ticket.reset();
      entry.pending_view_desc.reset();
      entry.pending_handle = VersionedBindlessHandle {};
      entry.texture = placeholder_texture_;
      entry.placeholder_texture = placeholder_texture_;
      InitiateAsyncLoad(resource_key, entry, "evicted_entry");
    }
    // Preserve per-resource stable indices. On failure, the descriptor is
    // repointed to the error texture, but the shader-visible handle remains
    // the entry's SRV index.
    return it->second.srv_index;
  }

  DLOG_SCOPE_F(4, "GetOrAllocate (allocate)");
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

  auto handle
    = allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_SRV);
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
    entry.descriptor_handle = VersionedBindlessHandle {};

    texture_map_.emplace(resource_key, std::move(entry));
    DLOG_F(
      3, "allocated: descriptor allocation failed -> cached error texture");
    return error_text_svi_;
  }

  const auto descriptor_index = handle.GetBindlessHandle();
  EnsureGenerationCapacity_(descriptor_index);
  const auto generation = slot_generations_.Load(descriptor_index);
  entry.descriptor_handle
    = VersionedBindlessHandle { descriptor_index, generation };
  DLOG_F(4, "descriptor_index: {}", descriptor_index);

  entry.srv_index
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(handle).get());

  registry.Register(entry.texture);
  registry.RegisterView(*entry.texture, std::move(handle), view_desc);
  BumpContentRevision(entry.srv_index);

  // Insert before initiating the load to ensure completion callbacks can
  // always resolve the entry even if the load completes synchronously.
  const auto result_index = entry.srv_index;
  auto [insert_it, inserted]
    = texture_map_.emplace(resource_key, std::move(entry));
  DCHECK_F(inserted);

  // Initiate async load using the opaque ResourceKey.
  InitiateAsyncLoad(resource_key, insert_it->second, "allocate_entry");

  DLOG_F(4, "Allocated SRV index {} for resource key {}", result_index,
    resource_key);

  DLOG_F(4, "srv_index: {}", result_index);

  return result_index;
}

//=== TextureBinder::Impl Implementation =====================================//

TextureBinder::Impl::Impl(const observer_ptr<Graphics> gfx,
  const observer_ptr<ProviderT> staging_provider,
  const observer_ptr<CoordinatorT> uploader,
  const observer_ptr<content::IAssetLoader> texture_loader, UploadLimits limits)
  : gfx_(gfx)
  , uploader_(uploader)
  , staging_provider_(staging_provider)
  , texture_loader_(texture_loader)
  , limits_(limits)
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  CHECK_NOTNULL_F(texture_loader_, "IAssetLoader cannot be null");

  CHECK_GT_F(limits_.max_completion_visits_per_frame, 0U);
  CHECK_GT_F(limits_.max_upload_visits_per_frame, 0U);
  CHECK_GT_F(limits_.max_eviction_visits_per_frame, 0U);
  if (limits_.max_upload_bytes_per_frame == 0U) {
    limits_.max_upload_bytes_per_frame = 1U;
  }
  if (limits_.max_pending_upload_bytes == 0U) {
    limits_.max_pending_upload_bytes = limits_.max_upload_bytes_per_frame;
  }
  if (limits_.deferred_retry_low_watermark_bytes == 0U
    || limits_.deferred_retry_low_watermark_bytes
      > limits_.max_pending_upload_bytes) {
    limits_.deferred_retry_low_watermark_bytes
      = (std::max)(std::size_t { 1U }, limits_.max_pending_upload_bytes / 2U);
  }
  if (limits_.max_deferred_retries_per_frame == 0U) {
    limits_.max_deferred_retries_per_frame = 1U;
  }

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

      LOG_F(2, "Eviction for {} (reason={})", event.key, event.reason);

      std::scoped_lock eviction_lock(eviction_mutex_);
      pending_evictions_.push_back(PendingEviction {
        .key = event.key,
        .reason = event.reason,
        .handle = {},
      });
      has_pending_evictions_.store(true, std::memory_order_release);
    });

  error_texture_ = CreateErrorTexture();
  CHECK_NOTNULL_F(error_texture_, "Failed to create error texture");

  auto& registry = gfx_->GetResourceRegistry();
  auto& allocator = gfx_->GetDescriptorAllocator();

  const auto error_view_desc
    = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});

  auto error_handle
    = allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_SRV);
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
      = allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
        graphics::ResourceViewType::kTexture_SRV);
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

  if (gfx_ != nullptr) {
    auto& registry = gfx_->GetResourceRegistry();
    auto& reclaimer = gfx_->GetDeferredReclaimer();

    auto release_shared_texture
      = [&](std::shared_ptr<graphics::Texture>& texture) -> void {
      if (!texture) {
        return;
      }
      if (registry.Contains(*texture)) {
        registry.UnRegisterResource(*texture);
      }
      reclaimer.RegisterDeferredRelease(std::move(texture));
    };

    if (placeholder_texture_ == error_texture_) {
      release_shared_texture(error_texture_);
      placeholder_texture_.reset();
    } else {
      release_shared_texture(placeholder_texture_);
      release_shared_texture(error_texture_);
    }
  } else {
    placeholder_texture_.reset();
    error_texture_.reset();
  }

  LOG_SCOPE_F(INFO, "TextureBinder Statistics");
  LOG_F(INFO, "lifecycle.generation_bumps : {}", lifecycle_generation_bumps_);
  LOG_F(
    INFO, "lifecycle.stale_discards   : {}", lifecycle_stale_discard_count_);
  LOG_F(INFO, "lifecycle.async_enqueued   : {}", lifecycle_async_enqueued_);
  LOG_F(INFO, "lifecycle.async_deferrals  : {}",
    lifecycle_async_backlog_deferrals_);
  LOG_F(INFO, "lifecycle.async_retries    : {}", lifecycle_async_retries_);
  LOG_F(INFO, "GetOrAllocate calls  : {}", total_get_or_allocate_calls_);
  LOG_F(INFO, "upload submissions   : {}", total_upload_submissions_);
  LOG_F(INFO, "cache hits     : {}", cache_hits_);
  LOG_F(INFO, "load failures  : {}", load_failures_);
  LOG_F(INFO, "textures loaded: {}", texture_map_.size());
}

auto TextureBinder::Impl::OnFrameStart() -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  frame_resources_ensured_ = false;
  ProcessEvictions();
  DiscardStaleQueuedUploads();
  PublishCompletedUploads();
}

auto TextureBinder::Impl::EnsureFrameResources() -> void
{
  if (frame_resources_ensured_) {
    return;
  }
  frame_resources_ensured_ = true;
  SubmitQueuedTextureUploads(limits_.max_upload_bytes_per_frame);
  ReissueDeferredLoads(limits_.max_deferred_retries_per_frame);
}

auto TextureBinder::Impl::PublishCompletedUploads() -> void
{
#ifdef OXYGEN_WITH_TRACY
  static const profiling::CpuProfileScopeDesc kScope { .label
    = "Vortex.Texture.PublishCompletedUploads",
    .category = profiling::ProfileCategory::kUpload };
  const profiling::CpuProfileScope profile(kScope);
#endif
  // Drain completed upload tickets and perform SRV repointing on the render
  // thread. This keeps descriptor updates serialized with other render-thread
  // mutations and relies on UploadCoordinator as the authoritative source of
  // upload completion.
  if (!uploader_) {
    return;
  }

  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();

  const auto visits = std::min(
    pending_completions_.size(), limits_.max_completion_visits_per_frame);
  for (std::size_t visit = 0; visit < visits; ++visit) {
    const auto work = pending_completions_.begin();
    bool handled = false;
    const ScopeGuard remove_completed([&]() noexcept {
      if (handled) {
        pending_completions_.erase(work);
      }
    });
    const auto resource_key = work->key;
    const auto found = texture_map_.find(resource_key);
    if (found == texture_map_.end()) {
      handled = true;
      continue;
    }
    auto& entry = found->second;
    if (!entry.pending_ticket || entry.pending_ticket->Id() != work->ticket_id
      || entry.pending_handle != work->handle) {
      handled = true;
      continue;
    }

    if (entry.evicted
      || entry.pending_handle != CurrentDescriptorHandle_(entry)) {
      ++lifecycle_stale_discard_count_;
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
      entry.pending_handle = VersionedBindlessHandle {};
      handled = true;
      continue;
    }

    if (entry.eviction_pending) {
      pending_completions_.splice(
        pending_completions_.end(), pending_completions_, work);
      continue;
    }
    const auto ticket = *entry.pending_ticket;
    const auto maybe_result = ticket.TryGetResult();
    if (!maybe_result.has_value()) {
      pending_completions_.splice(
        pending_completions_.end(), pending_completions_, work);
      continue;
    }

    DLOG_SCOPE_F(4, "Upload completion");
    DLOG_F(4, "resource: {}", resource_key);
    DLOG_F(4, "ticket: {}", ticket.Id());
    DLOG_F(
      4, "descriptor_index: {}", entry.descriptor_handle.ToBindlessHandle());
    DLOG_F(4, "is_placeholder: {}", entry.is_placeholder);
    DLOG_F(4, "load_failed: {}", entry.load_failed);

    DLOG_F(2, "Upload ticket {} completed for resource key {}", ticket.Id(),
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
        ticket.Id());

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
      if (!entry.descriptor_handle.IsValid() || !entry.pending_view_desc) {
        entry.load_failed = true;
        entry.is_placeholder = true;
      } else {
        const bool release_placeholder = entry.placeholder_texture
          && entry.placeholder_texture != entry.texture
          && entry.placeholder_texture != placeholder_texture_
          && entry.placeholder_texture != error_texture_;
        auto retirement
          = graphics::detail::DeferredReclaimer::PreparedDeferredAction {};
        if (release_placeholder) {
          retirement = reclaimer.PrepareDeferredAction(
            [texture = entry.placeholder_texture]() mutable {
              texture.reset();
            });
        }
        const bool updated = registry.UpdateView(*entry.texture,
          entry.descriptor_handle.ToBindlessHandle(), *entry.pending_view_desc);
        if (!updated) {
          entry.load_failed = true;
          entry.is_placeholder = true;
          ReleaseTextureNextFrame(
            registry, reclaimer, std::move(entry.texture));
          entry.texture = entry.placeholder_texture;
        } else {
          if (release_placeholder) {
            registry.UnRegisterResource(*entry.placeholder_texture);
            reclaimer.CommitDeferredAction(std::move(retirement));
            entry.placeholder_texture.reset();
          }
          BumpContentRevision(entry.srv_index);
          entry.is_placeholder = false;
          entry.load_failed = false;
        }
      }
    }

    // Clear pending ticket and view desc after handling
    entry.pending_ticket.reset();
    entry.pending_view_desc.reset();
    entry.pending_handle = VersionedBindlessHandle {};
    handled = true;
  }
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

  std::ranges::sort(records,
    [](const Record& a, const Record& b) -> bool { return a.bytes > b.bytes; });

  const auto emit_count = (std::min)(records.size(), top_n);

  LOG_F(INFO,
    "Estimated GPU texture memory: total={} across {} textures (top {} shown)",
    PrettyBytes(total_bytes).c_str(), count, emit_count);

  for (std::size_t i = 0U; i < emit_count; ++i) {
    const auto& r = records.at(i);
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

    constexpr std::array white_pixel_data {
      static_cast<std::byte>(0xFF),
      static_cast<std::byte>(0xFF),
      static_cast<std::byte>(0xFF),
      static_cast<std::byte>(0xFF),
    };
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
auto TextureBinder::Impl::InitiateAsyncLoad(content::ResourceKey resource_key,
  TextureEntry& entry, const char* reason) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DLOG_SCOPE_F(3, "TextureBinder InitiateAsyncLoad");
  DLOG_F(3, "resource: {}", resource_key);
  LOG_F(INFO,
    "Initiating async load for resource key: {} (reason={}, gen={}, "
    "pending={}, placeholder={}, evicted={}, last_eviction_reason={})",
    resource_key, reason,
    CurrentDescriptorHandle_(entry).GenerationValue().get(),
    entry.pending_ticket.has_value(), entry.is_placeholder, entry.evicted,
    entry.last_eviction_reason);

  const auto handle = CurrentDescriptorHandle_(entry);

  texture_loader_->StartLoadTexture(resource_key,
    [gate = callback_gate_, this, resource_key, handle](
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

      this->OnTextureResourceLoaded(resource_key, handle, std::move(tex_res));
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
 @param handle Versioned descriptor handle captured when the load request was
   issued.
 @param tex_res Loaded texture resource, or `nullptr` on load failure.
*/
auto TextureBinder::Impl::OnTextureResourceLoaded(
  const content::ResourceKey resource_key, const VersionedBindlessHandle handle,
  // NOLINTNEXTLINE(*-unnecessary-value-param) - moved from a lambda capture
  std::shared_ptr<data::TextureResource> tex_res) -> void
{
  // This callback may execute off the render thread. Do not touch render
  // thread-owned state here (e.g. texture_map_, SRV descriptors).
  bool deferred = false;
  if (tex_res) {
    const auto data_bytes = tex_res->GetDataSize();
    std::scoped_lock lock(pending_uploads_mutex_);
    const bool would_exceed_backlog_cap = pending_upload_bytes_ != 0U
      && pending_upload_bytes_ + data_bytes > limits_.max_pending_upload_bytes;
    if (!would_exceed_backlog_cap) {
      pending_upload_bytes_ += data_bytes;
      pending_uploads_.push_back(PendingUpload {
        .key = resource_key,
        .resource = std::move(tex_res),
        .handle = handle,
      });
    } else {
      deferred = true;
    }
  } else {
    std::scoped_lock lock(pending_uploads_mutex_);
    pending_uploads_.push_back(PendingUpload {
      .key = resource_key,
      .resource = nullptr,
      .handle = handle,
    });
  }

  if (deferred) {
    std::scoped_lock lock(deferred_retries_mutex_);
    if (deferred_retry_keys_.insert(resource_key).second) {
      deferred_retries_.push_back(DeferredRetry {
        .key = resource_key,
        .handle = handle,
      });
    }
    ++lifecycle_async_backlog_deferrals_;
    LOG_F(WARNING,
      "Deferring decoded texture {} because pending upload backlog would "
      "exceed "
      "{} bytes",
      resource_key, limits_.max_pending_upload_bytes);
  } else {
    ++lifecycle_async_enqueued_;
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

auto TextureBinder::Impl::EnsureGenerationCapacity_(
  const bindless::HeapIndex descriptor_index) -> void
{
  const auto raw_index = descriptor_index.get();
  if (raw_index == kInvalidBindlessHeapIndex.get()) {
    return;
  }
  const auto needed_capacity = raw_index + 1U;
  if (needed_capacity <= slot_generation_capacity_) {
    return;
  }
  slot_generations_.Resize(bindless::Capacity { needed_capacity });
  slot_generation_capacity_ = needed_capacity;
}

auto TextureBinder::Impl::CurrentGeneration_(const TextureEntry& entry) const
  -> bindless::Generation
{
  if (!entry.descriptor_handle.IsValid()) {
    return bindless::Generation { 0U };
  }
  return slot_generations_.Load(entry.descriptor_handle.ToBindlessHandle());
}

auto TextureBinder::Impl::CurrentDescriptorHandle_(
  const TextureEntry& entry) const -> VersionedBindlessHandle
{
  if (!entry.descriptor_handle.IsValid()) {
    return VersionedBindlessHandle {};
  }
  return VersionedBindlessHandle { entry.descriptor_handle.ToBindlessHandle(),
    CurrentGeneration_(entry) };
}

auto TextureBinder::Impl::DiscardStaleQueuedUploads() -> void
{
  std::size_t visits = 0U;
  {
    const std::scoped_lock lock(pending_uploads_mutex_);
    visits
      = std::min(pending_uploads_.size(), limits_.max_upload_visits_per_frame);
  }
  for (std::size_t visit = 0U; visit < visits; ++visit) {
    PendingUpload discarded;
    {
      const std::scoped_lock lock(pending_uploads_mutex_);
      // A sweep ends after its original item count, even if producers keep
      // appending. Otherwise new arrivals can starve an evicted earlier item.
      if (decoded_cleanup_remaining_ == 0U
        || decoded_cleanup_cursor_ == pending_uploads_.end()) {
        decoded_cleanup_cursor_ = pending_uploads_.begin();
        decoded_cleanup_remaining_ = pending_uploads_.size();
      }
      const auto work = decoded_cleanup_cursor_++;
      --decoded_cleanup_remaining_;
      const auto entry = texture_map_.find(work->key);
      if (entry == texture_map_.end() || entry->second.evicted
        || entry->second.eviction_pending
        || work->handle != CurrentDescriptorHandle_(entry->second)) {
        discarded = std::move(*work);
        if (discarded.resource) {
          pending_upload_bytes_ -= std::min(
            pending_upload_bytes_, discarded.resource->GetDataSize());
        }
        pending_uploads_.erase(work);
      }
    }
    // Destroy decoded storage outside the producer mutex.
  }
}

auto TextureBinder::Impl::SubmitQueuedTextureUploads(
  const std::size_t max_bytes) -> void
{
  DCHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  std::size_t submitted_bytes = 0U;

  for (std::size_t visit = 0; visit < limits_.max_upload_visits_per_frame;
    ++visit) {
    if (submitted_bytes >= max_bytes) {
      return;
    }

    PendingUpload pending;
    std::size_t pending_bytes = 0U;
    {
      std::scoped_lock lock(pending_uploads_mutex_);
      if (pending_uploads_.empty()) {
        return;
      }
      const auto& next = pending_uploads_.front();
      if (next.resource && submitted_bytes != 0U
        && next.resource->GetDataSize() > max_bytes - submitted_bytes) {
        return;
      }
      if (decoded_cleanup_cursor_ == pending_uploads_.begin()) {
        ++decoded_cleanup_cursor_;
      }
      pending = std::move(pending_uploads_.front());
      pending_uploads_.pop_front();
      if (pending.resource) {
        pending_bytes = pending.resource->GetDataSize();
        pending_upload_bytes_
          -= (std::min)(pending_upload_bytes_, pending_bytes);
      }
    }

    TextureEntry* entry_ptr = this->FindEntryOrLog(pending.key);
    if (entry_ptr == nullptr) {
      continue;
    }
    auto& entry = *entry_ptr;

    if (entry.evicted || entry.eviction_pending
      || pending.handle != CurrentDescriptorHandle_(entry)) {
      ++lifecycle_stale_discard_count_;
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
        "Texture {} requires {} bytes; exceeds per-frame budget {}. Submitting "
        "anyway.",
        pending.key, data_bytes, max_bytes);
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
    // The CPU payload is released after upload; measure while it is here.
    entry.cube_illuminance = MeasureCubeIlluminance(*pending.resource);
    if (std::holds_alternative<PrepareTexture2DUploadFailure>(
          prepared_result)) {
      const auto& failure
        = std::get<PrepareTexture2DUploadFailure>(prepared_result);
      switch (failure.reason) {
      case PrepareTexture2DUploadFailure::Reason::kUnsupportedTextureType:
        LOG_F(ERROR,
          "Texture async upload only supports 2D textures, 2D arrays, and "
          "cubemaps");
        break;
      case PrepareTexture2DUploadFailure::Reason::kUnsupportedFormat:
        LOG_F(
          ERROR, "Texture upload only supports uncompressed and BC7 formats");
        break;
      case PrepareTexture2DUploadFailure::Reason::kUnsupportedDepth:
        LOG_F(ERROR, "Texture async upload only supports 2D textures");
        break;
      case PrepareTexture2DUploadFailure::Reason::kCreateTextureException:
        LOG_F(ERROR, "CreateTexture threw during async load");
        break;
      case PrepareTexture2DUploadFailure::Reason::kCreateTextureReturnedNull:
        LOG_F(ERROR, "CreateTexture returned null during async load");
        break;
      case PrepareTexture2DUploadFailure::Reason::kLayoutFailure: {
        if (!failure.layout_failure.has_value()) {
          LOG_F(ERROR, "Texture upload layout validation failed");
          break;
        }
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

auto TextureBinder::Impl::ReissueDeferredLoads(const std::size_t max_retries)
  -> void
{
  for (std::size_t attempts = 0; attempts < max_retries; ++attempts) {
    {
      std::scoped_lock lock(pending_uploads_mutex_);
      if (pending_upload_bytes_ >= limits_.deferred_retry_low_watermark_bytes) {
        return;
      }
    }

    DeferredRetry retry {};
    {
      std::scoped_lock lock(deferred_retries_mutex_);
      if (deferred_retries_.empty()) {
        return;
      }
      retry = deferred_retries_.front();
      deferred_retries_.pop_front();
      deferred_retry_keys_.erase(retry.key);
    }

    auto* entry_ptr = FindEntryOrLog(retry.key);
    if (entry_ptr == nullptr) {
      continue;
    }
    auto& entry = *entry_ptr;
    if (entry.evicted || entry.eviction_pending
      || retry.handle != CurrentDescriptorHandle_(entry)
      || entry.pending_ticket.has_value()) {
      continue;
    }

    ++lifecycle_async_retries_;
    InitiateAsyncLoad(retry.key, entry, "deferred_backlog_retry");
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
auto TextureBinder::Impl::AcceptEvictions() -> void
{
  if (!has_pending_evictions_.load(std::memory_order_acquire)) {
    return;
  }
  std::list<PendingEviction>::iterator first;
  {
    const std::scoped_lock lock(eviction_mutex_);
    if (pending_evictions_.empty()) {
      has_pending_evictions_.store(false, std::memory_order_release);
      return;
    }
    first = pending_evictions_.begin();
    eviction_work_.splice(eviction_work_.end(), pending_evictions_);
    has_pending_evictions_.store(false, std::memory_order_release);
  }
  for (auto it = first; it != eviction_work_.end();) {
    const auto entry = texture_map_.find(it->key);
    if (entry == texture_map_.end() || entry->second.evicted) {
      it = eviction_work_.erase(it);
      continue;
    }
    it->handle = CurrentDescriptorHandle_(entry->second);
    entry->second.eviction_pending = true;
    ++it;
  }
}

auto TextureBinder::Impl::ProcessEvictions() -> void
{
  AcceptEvictions();
#ifdef OXYGEN_WITH_TRACY
  static const profiling::CpuProfileScopeDesc kScope { .label
    = "Vortex.Texture.ProcessEvictions",
    .category = profiling::ProfileCategory::kUpload };
  const profiling::CpuProfileScope profile(kScope);
#endif
  auto& registry = gfx_->GetResourceRegistry();
  auto& reclaimer = gfx_->GetDeferredReclaimer();
  const auto visits
    = std::min(eviction_work_.size(), limits_.max_eviction_visits_per_frame);
  for (std::size_t visit = 0; visit < visits; ++visit) {
    const auto work = eviction_work_.begin();
    const auto found = texture_map_.find(work->key);
    if (found == texture_map_.end() || found->second.evicted
      || work->handle != CurrentDescriptorHandle_(found->second)) {
      eviction_work_.erase(work);
      continue;
    }
    auto& entry = found->second;
    if (!entry.resident_lease.expired()) {
      eviction_work_.splice(eviction_work_.end(), eviction_work_, work);
      continue;
    }

    const auto owned
      = [this](const std::shared_ptr<graphics::Texture>& texture) {
          return texture && texture != placeholder_texture_
            && texture != error_texture_;
        };
    const auto old_texture = owned(entry.texture) ? entry.texture : nullptr;
    const auto old_placeholder = owned(entry.placeholder_texture)
        && entry.placeholder_texture != old_texture
      ? entry.placeholder_texture
      : nullptr;
    const auto prepare = [&reclaimer](
                           const std::shared_ptr<graphics::Texture>& texture) {
      if (!texture) {
        return graphics::detail::DeferredReclaimer::PreparedDeferredAction {};
      }
      return reclaimer.PrepareDeferredAction(
        [retained = texture]() mutable { retained.reset(); });
    };
    auto release_texture = prepare(old_texture);
    auto release_placeholder = prepare(old_placeholder);

    if (entry.descriptor_handle.IsValid()) {
      const auto description
        = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});
      if (!registry.UpdateView(*placeholder_texture_,
            entry.descriptor_handle.ToBindlessHandle(), description)) {
        entry.load_failed = true;
        eviction_work_.splice(eviction_work_.end(), eviction_work_, work);
        continue;
      }
      BumpContentRevision(entry.srv_index);
      entry.is_placeholder = true;
    }
    if (old_texture) {
      registry.UnRegisterResource(*old_texture);
    }
    if (old_placeholder) {
      registry.UnRegisterResource(*old_placeholder);
    }
    reclaimer.CommitDeferredAction(std::move(release_texture));
    reclaimer.CommitDeferredAction(std::move(release_placeholder));

    entry.evicted = true;
    entry.eviction_pending = false;
    entry.last_eviction_reason = work->reason;
    if (entry.descriptor_handle.IsValid()) {
      const auto index = entry.descriptor_handle.ToBindlessHandle();
      slot_generations_.Bump(index);
      ++lifecycle_generation_bumps_;
      entry.descriptor_handle
        = VersionedBindlessHandle { index, slot_generations_.Load(index) };
    }
    entry.pending_handle = {};
    entry.pending_ticket.reset();
    entry.pending_view_desc.reset();
    entry.texture = placeholder_texture_;
    entry.placeholder_texture = placeholder_texture_;
    entry.is_placeholder = true;
    entry.load_failed = false;
    eviction_work_.erase(work);
  }
}

auto TextureBinder::Impl::SubmitTextureUpload(
  const content::ResourceKey resource_key, TextureEntry& entry,
  const graphics::TextureDesc& desc,
  std::shared_ptr<graphics::Texture>&& new_texture,
  std::vector<vortex::upload::UploadSubresource>&& dst_subresources,
  vortex::upload::UploadTextureSourceView&& src_view,
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

  if (entry.evicted || entry.eviction_pending) {
    DLOG_F(4, "Discarding texture upload submission for evicted resource {}",
      resource_key);
    return;
  }

  if (trailing_bytes != 0U) {
    LOG_F(INFO, "TextureResource had {} trailing bytes after planned upload",
      trailing_bytes);
  }

  vortex::upload::UploadRequest req {
    .kind = vortex::upload::UploadKind::kTexture2D,
    .debug_name = desc.debug_name,
    .desc = vortex::upload::UploadTextureDesc {
      .dst = new_texture,
      .width = desc.width,
      .height = desc.height,
      .depth = desc.depth,
      .format = desc.format,
    },
    .subresources = std::move(dst_subresources),
    .data = std::move(src_view),
  };

  const auto work = pending_completions_.emplace(pending_completions_.end(),
    PendingCompletion { .key = resource_key,
      .handle = CurrentDescriptorHandle_(entry),
      .ticket_id = vortex::upload::TicketId { 0 } });
  bool tracked = false;
  const ScopeGuard rollback([&]() noexcept {
    if (!tracked) {
      pending_completions_.erase(work);
    }
  });
  const auto upload_result = uploader_->Submit(req, *staging_provider_);
  if (!upload_result) {
    const auto error_code
      = oxygen::vortex::upload::make_error_code(upload_result.error());
    LOG_F(ERROR, "Texture upload failed ({}): {}", desc.debug_name,
      error_code.message());

    // Upload submission failure: keep the placeholder bound.
    this->HandleLoadFailure(resource_key, entry,
      FailurePolicy::kKeepPlaceholderBound, std::move(new_texture));
    return;
  }

  ++total_upload_submissions_;

  DLOG_F(3, "ticket: {}", upload_result->Id());

  // Register the created texture so the ResourceRegistry can manage it and
  // allow us to UpdateView later when upload completes.
  auto& registry = gfx_->GetResourceRegistry();
  registry.Register(new_texture);

  const auto view_desc = MakeTextureSrvViewDesc(desc.format,
    MipRange { .base_mip_level = 0U, .num_mip_levels = desc.mip_levels },
    ArrayRange { .base_array_slice = 0U, .num_array_slices = desc.array_size });

  // Store pending ticket + view desc for OnFrameStart() to observe; also
  // set the entry.texture now so UpdateView can target it when complete.
  work->ticket_id = upload_result->Id();
  entry.pending_ticket = *upload_result;
  entry.pending_handle = CurrentDescriptorHandle_(entry);
  entry.pending_view_desc = view_desc;
  entry.texture = std::move(new_texture);
  entry.is_placeholder = true;
  entry.load_failed = false;
  tracked = true;
  DLOG_F(3, "InitiateAsyncLoad: submitted upload ticket {} for resource {}",
    entry.pending_ticket->Id(), resource_key);
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
  DLOG_F(3, "descriptor_index: {}", entry.descriptor_handle.ToBindlessHandle());
  DLOG_F(3, "is_placeholder: {}", entry.is_placeholder);
  DLOG_F(3, "load_failed: {}", entry.load_failed);
  DLOG_F(3, "releasing_new_texture: {}", static_cast<bool>(texture_to_release));

  ++load_failures_;
  entry.load_failed = true;
  entry.pending_ticket.reset();
  entry.pending_view_desc.reset();
  entry.pending_handle = VersionedBindlessHandle {};

  if (texture_to_release) {
    ReleaseTextureNextFrame(gfx_->GetResourceRegistry(),
      gfx_->GetDeferredReclaimer(), std::move(texture_to_release));
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
  if (!entry.descriptor_handle.IsValid()) {
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
  DLOG_F(3, "descriptor_index: {}", entry.descriptor_handle.ToBindlessHandle());

  auto& registry = gfx_->GetResourceRegistry();
  const auto view_desc = MakeTextureSrvViewDesc(Format::kRGBA8UNorm, {}, {});
  const bool updated = registry.UpdateView(
    *error_texture_, entry.descriptor_handle.ToBindlessHandle(), view_desc);
  if (!updated) {
    LOG_F(ERROR, "Failed to repoint descriptor to error texture for {}",
      resource_key);
    return false;
  }

  BumpContentRevision(entry.srv_index);
  LOG_F(INFO, "Repointed descriptor {} to error texture for resource {}",
    entry.descriptor_handle.ToBindlessHandle(), resource_key);
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
    LOG_F(ERROR, "Texture upload only supports non-BC formats");
    return;
  }

  if (desc.depth != 1U) {
    LOG_F(ERROR, "Texture upload only supports Texture2D");
    return;
  }

  const auto bytes_per_row = static_cast<std::uint32_t>(desc.width)
    * static_cast<std::uint32_t>(format_info.bytes_per_block);
  const auto expected_bytes = static_cast<std::size_t>(bytes_per_row)
    * static_cast<std::size_t>(desc.height);
  if (data.size() != expected_bytes) {
    LOG_F(ERROR, "Texture upload expected {} bytes for {}x{}, got {}",
      expected_bytes, desc.width, desc.height, data.size());
    return;
  }

  vortex::upload::UploadTextureSourceView src_view;
  src_view.subresources.push_back({
    .bytes = data,
    .row_pitch = bytes_per_row,
    .slice_pitch = bytes_per_row * desc.height,
  });

  vortex::upload::UploadRequest req {
    .kind = vortex::upload::UploadKind::kTexture2D,
    .debug_name = debug_name,
    .desc = vortex::upload::UploadTextureDesc {
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
      = oxygen::vortex::upload::make_error_code(result.error());
    LOG_F(ERROR, "Texture upload failed ({}): {}", debug_name,
      error_code.message());
  }
}

auto TextureBinder::Impl::GetPendingUploadCount() const noexcept -> std::size_t
{
  std::scoped_lock lock(pending_uploads_mutex_);
  return pending_uploads_.size();
}

auto TextureBinder::Impl::GetPendingUploadBytes() const noexcept -> std::size_t
{
  std::scoped_lock lock(pending_uploads_mutex_);
  return pending_upload_bytes_;
}

auto TextureBinder::Impl::GetDeferredRetryCount() const noexcept -> std::size_t
{
  std::scoped_lock lock(deferred_retries_mutex_);
  return deferred_retries_.size();
}

auto TextureBinder::Impl::GetPendingUploadByteBudget() const noexcept
  -> std::size_t
{
  return limits_.max_pending_upload_bytes;
}

} // namespace oxygen::vortex::resources
