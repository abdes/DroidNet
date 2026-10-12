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
#include <stdexcept>
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
#include <Oxygen/Graphics/Common/Graphics.h>
#include <Oxygen/Graphics/Common/ResourceRegistry.h>
#include <Oxygen/Graphics/Common/Texture.h>
#include <Oxygen/Graphics/Common/Types/DescriptorVisibility.h>
#include <Oxygen/Graphics/Common/Types/ResourceStates.h>
#include <Oxygen/Graphics/Common/Types/ResourceViewType.h>
#include <Oxygen/Nexus/IndexReuse.h>
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
    if (a > std::numeric_limits<std::size_t>::max() - b) {
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

        if (blocks_x > std::numeric_limits<std::size_t>::max() / bpb) {
          return std::nullopt;
        }
        const auto row_bytes = blocks_x * bpb;
        if (blocks_y > 0U
          && row_bytes > std::numeric_limits<std::size_t>::max() / blocks_y) {
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

        if (blocks_x > std::numeric_limits<std::size_t>::max() / bpb) {
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
          && row_pitch > std::numeric_limits<std::size_t>::max() / blocks_y) {
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

  //! What an entry without a resident version publishes.
  enum class FallbackKind : std::uint8_t {
    kPlaceholder,
    kError,
  };

  //! Fallback dimension; a shader must read a view of the type it declares.
  enum class FallbackShape : std::uint8_t {
    k2D,
    kCube,
    k2DArray,
  };

  constexpr std::array kFallbackShapes {
    FallbackShape::k2D,
    FallbackShape::kCube,
    FallbackShape::k2DArray,
  };

  [[nodiscard]] constexpr auto ShapeOf(const TextureType type) noexcept
    -> FallbackShape
  {
    if (type == TextureType::kTextureCube) {
      return FallbackShape::kCube;
    }
    if (type == TextureType::kTexture2DArray) {
      return FallbackShape::k2DArray;
    }
    return FallbackShape::k2D;
  }

  [[nodiscard]] auto MakeFallbackDesc(
    const FallbackKind kind, const FallbackShape shape) -> graphics::TextureDesc
  {
    constexpr std::uint32_t kCubeFaceCount = 6U;
    constexpr std::uint32_t kErrorExtentPx = 64U;

    graphics::TextureDesc desc;
    desc.format = Format::kRGBA8UNorm;
    desc.width = kind == FallbackKind::kError ? kErrorExtentPx : 1U;
    desc.height = desc.width;
    desc.depth = 1U;
    desc.mip_levels = 1U;
    desc.array_size = 1U;
    desc.is_shader_resource = true;
    desc.initial_state = graphics::ResourceStates::kCommon;
    std::string name = kind == FallbackKind::kError
      ? "TextureBinder.Error"
      : "TextureBinder.Placeholder";
    switch (shape) {
    case FallbackShape::k2D:
      desc.texture_type = TextureType::kTexture2D;
      name += "2D";
      break;
    case FallbackShape::kCube:
      desc.texture_type = TextureType::kTextureCube;
      desc.array_size = kCubeFaceCount;
      name += "Cube";
      break;
    case FallbackShape::k2DArray:
      desc.texture_type = TextureType::kTexture2DArray;
      name += "Array";
      break;
    }
    desc.debug_name = std::move(name);
    return desc;
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
  //! Entry identity. Async work captures it; an eviction retires it, so late
  //! callbacks for an earlier load cannot touch the reloaded entry.
  using EntryHandle = nexus::VersionedIndex<std::uint32_t>;

  enum class FailurePolicy : uint8_t {
    kBindErrorTexture,
    kKeepPlaceholderBound,
  };

  struct FallbackBinding {
    std::shared_ptr<graphics::Texture> texture;
    ShaderVisibleIndex srv { kInvalidShaderVisibleIndex };
  };

  struct TextureEntry {
    EntryHandle handle;
    //! Last known type of the resource; selects the fallback shape.
    TextureType texture_type { TextureType::kTexture2D };
    bool load_failed { false };
    bool evicted { false };
    bool eviction_pending { false };
    std::weak_ptr<const ReadyTexture> resident_lease;
    //! Upward illuminance of a float cube, measured from its CPU payload.
    std::optional<float> cube_illuminance;

    std::optional<vortex::upload::UploadTicket> pending_ticket;
    //! Destination of the in-flight upload; published only on completion.
    std::shared_ptr<graphics::Texture> pending_texture;
    //! The resident version, or null while a fallback is published.
    std::shared_ptr<graphics::Texture> texture;

    content::EvictionReason last_eviction_reason {
      content::EvictionReason::kRefCountZero,
    };

    //! The index GetOrAllocate() publishes for this entry.
    ShaderVisibleIndex srv_index { kInvalidShaderVisibleIndex };
  };

  struct CallbackGate {
    std::mutex mutex;
    bool alive { true };
  };

  struct PendingUpload {
    content::ResourceKey key;
    std::shared_ptr<data::TextureResource> resource;
    EntryHandle handle;
  };

  struct PendingCompletion {
    content::ResourceKey key;
    EntryHandle handle;
    vortex::upload::TicketId ticket_id { 0 };
  };

  struct DeferredRetry {
    content::ResourceKey key;
    EntryHandle handle;
  };

  struct PendingEviction {
    content::ResourceKey key;
    content::EvictionReason reason { content::EvictionReason::kRefCountZero };
    EntryHandle handle;
  };

  [[nodiscard]] auto Fallback(FallbackKind kind, FallbackShape shape) const
    -> const FallbackBinding&;
  [[nodiscard]] auto FallbackIndex(
    FallbackKind kind, const TextureEntry& entry) const -> ShaderVisibleIndex;
  auto CreateFallback(FallbackKind kind, FallbackShape shape)
    -> FallbackBinding;
  //! Registers a shader-visible view of `texture` in a new descriptor.
  //! Throws on failure, leaving nothing registered.
  auto RegisterVersion(graphics::Texture& texture) -> ShaderVisibleIndex;
  auto Publish(TextureEntry& entry, ShaderVisibleIndex srv) -> void;
  auto ActivateEntry() -> EntryHandle;
  auto RetireEntryHandle(TextureEntry& entry) noexcept -> void;
  [[nodiscard]] auto IsCurrent(
    const TextureEntry& entry, EntryHandle handle) const noexcept -> bool;

  auto InitiateAsyncLoad(content::ResourceKey resource_key,
    const TextureEntry& entry, const char* reason) -> void;

  auto OnTextureResourceLoaded(content::ResourceKey resource_key,
    EntryHandle handle, std::shared_ptr<data::TextureResource> tex_res) -> void;

  auto DiscardStaleQueuedUploads() -> void;
  auto SubmitQueuedTextureUploads(std::size_t max_bytes) -> void;
  auto ReissueDeferredLoads(std::size_t max_retries) -> void;

  auto HandleLoadFailure(content::ResourceKey resource_key, TextureEntry& entry,
    FailurePolicy policy) -> void;

  auto AcceptEvictions() -> void;
  auto ProcessEvictions() -> void;
  auto PublishCompletedUploads() -> void;

  auto FindEntryOrLog(content::ResourceKey resource_key) -> TextureEntry*;

  auto SubmitTextureUpload(content::ResourceKey resource_key,
    TextureEntry& entry, const graphics::TextureDesc& desc,
    std::shared_ptr<graphics::Texture>&& new_texture,
    std::vector<vortex::upload::UploadSubresource>&& dst_subresources,
    vortex::upload::UploadTextureSourceView&& src_view,
    std::size_t trailing_bytes) -> void;

  auto SubmitUniformLayers(const std::shared_ptr<graphics::Texture>& texture,
    std::span<const std::byte> layer_bytes) -> void;

  observer_ptr<Graphics> gfx_;
  observer_ptr<vortex::upload::UploadCoordinator> uploader_;
  observer_ptr<vortex::upload::StagingProvider> staging_provider_;
  observer_ptr<content::IAssetLoader> texture_loader_;
  UploadLimits limits_;
  bool frame_resources_ensured_ { false };
  std::list<PendingCompletion> pending_completions_;

  std::shared_ptr<CallbackGate> callback_gate_;

  std::unordered_map<content::ResourceKey, TextureEntry> texture_map_;
  // Entry identities are CPU-only: no GPU work reads them, so a retired
  // identity is finalized at once and its index reused immediately.
  nexus::IndexReuse<std::uint32_t> entry_reuse_;
  std::vector<std::uint32_t> free_entry_indices_;
  std::uint32_t next_entry_index_ { 0U };

  std::unordered_map<ShaderVisibleIndex, std::uint64_t> content_revisions_;
  // Source of descriptor revisions. A recycled descriptor slot never repeats
  // a revision it carried for an earlier texture.
  std::uint64_t next_content_revision_ { 0U };
  // Advances whenever an entry publishes a different index.
  std::uint64_t resident_content_revision_ { 0U };

  mutable std::mutex pending_uploads_mutex_;
  std::list<PendingUpload> pending_uploads_;
  std::list<PendingUpload>::iterator decoded_cleanup_cursor_ {
    pending_uploads_.end(),
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

  // Shared placeholder and error bindings, one per fallback shape.
  std::array<FallbackBinding, kFallbackShapes.size()> placeholders_;
  std::array<FallbackBinding, kFallbackShapes.size()> errors_;

  // Telemetry stats
  std::uint64_t total_get_or_allocate_calls_ { 0U };
  std::uint64_t total_upload_submissions_ { 0U };
  std::uint64_t cache_hits_ { 0U };
  std::uint64_t load_failures_ { 0U };
  std::uint64_t lifecycle_entry_retirements_ { 0U };
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

 `OnFrameEnd()` is intentionally a no-op: completions are published during
 `OnFrameStart()`, and retired versions are released by the graphics
 backend's `DeferredReclaimer` when their frame slot cycles.
*/
auto TextureBinder::OnFrameEnd() -> void { }

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
  return entry.texture != nullptr;
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
  const auto mips_of = [](const std::shared_ptr<graphics::Texture>& texture)
    -> std::optional<std::uint32_t> {
    return std::optional { texture->GetDescriptor().mip_levels };
  };
  if (resource_key.IsError()) {
    return mips_of(Fallback(FallbackKind::kError, FallbackShape::k2D).texture);
  }
  if (resource_key.IsFallback()) {
    return mips_of(
      Fallback(FallbackKind::kPlaceholder, FallbackShape::k2D).texture);
  }

  const auto it = texture_map_.find(resource_key);
  if (it == texture_map_.end()) {
    // The fast-path placeholder binding does not create entries.
    return std::nullopt;
  }

  const auto& entry = it->second;
  if (entry.texture) {
    return mips_of(entry.texture);
  }
  const auto kind
    = entry.load_failed ? FallbackKind::kError : FallbackKind::kPlaceholder;
  return mips_of(Fallback(kind, ShapeOf(entry.texture_type)).texture);
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
    return Fallback(FallbackKind::kPlaceholder, FallbackShape::k2D).srv;
  }
  if (resource_key.IsError()) {
    return Fallback(FallbackKind::kError, FallbackShape::k2D).srv;
  }

  AcceptEvictions();
  if (const auto it = texture_map_.find(resource_key);
    it != texture_map_.end()) {
    ++cache_hits_;
    auto& entry = it->second;
    // Cache hits can be extremely frequent (per-frame, per-material).
    DLOG_F(6, "GetOrAllocate: cache hit -> srv_index {}", entry.srv_index);
    if (entry.evicted) {
      LOG_F(INFO, "Reloading evicted resource {} (reason={})", resource_key,
        entry.last_eviction_reason);
      entry.handle = ActivateEntry();
      entry.evicted = false;
      entry.load_failed = false;
      InitiateAsyncLoad(resource_key, entry, "evicted_entry");
    }
    return entry.srv_index;
  }

  DLOG_SCOPE_F(4, "GetOrAllocate (allocate)");
  DLOG_F(4, "resource: {}", resource_key);

  TextureEntry entry;
  entry.handle = ActivateEntry();
  entry.srv_index
    = Fallback(FallbackKind::kPlaceholder, FallbackShape::k2D).srv;

  // Insert before initiating the load to ensure completion callbacks can
  // always resolve the entry even if the load completes synchronously.
  const auto [insert_it, inserted]
    = texture_map_.emplace(resource_key, std::move(entry));
  DCHECK_F(inserted);
  InitiateAsyncLoad(resource_key, insert_it->second, "allocate_entry");
  return insert_it->second.srv_index;
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
  CHECK_NOTNULL_F(gfx_, "Graphics cannot be null");
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

  for (const auto shape : kFallbackShapes) {
    const auto slot = static_cast<std::size_t>(shape);
    placeholders_.at(slot) = CreateFallback(FallbackKind::kPlaceholder, shape);
    errors_.at(slot) = CreateFallback(FallbackKind::kError, shape);
  }

  callback_gate_ = std::make_shared<CallbackGate>();
  eviction_subscription_ = texture_loader_->SubscribeResourceEvictions(
    data::TextureResource::ClassTypeId(),
    [gate = callback_gate_, this](const content::EvictionEvent& event) -> void {
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

  LOG_F(INFO, "TextureBinder initialized: placeholder SRV {}, error SRV {}",
    Fallback(FallbackKind::kPlaceholder, FallbackShape::k2D).srv,
    Fallback(FallbackKind::kError, FallbackShape::k2D).srv);
}

TextureBinder::Impl::~Impl()
{
  {
    std::scoped_lock lock(callback_gate_->mutex);
    callback_gate_->alive = false;
  }
  entry_reuse_.Close();

  for (auto& [key, entry] : texture_map_) {
    gfx_->RetireResource(entry.texture);
    gfx_->RetireResource(entry.pending_texture);
  }
  for (auto& binding : placeholders_) {
    gfx_->RetireResource(binding.texture);
  }
  for (auto& binding : errors_) {
    gfx_->RetireResource(binding.texture);
  }

  LOG_SCOPE_F(INFO, "TextureBinder Statistics");
  LOG_F(INFO, "lifecycle.entry_retirements: {}", lifecycle_entry_retirements_);
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

auto TextureBinder::Impl::Fallback(const FallbackKind kind,
  const FallbackShape shape) const -> const FallbackBinding&
{
  const auto slot = static_cast<std::size_t>(shape);
  return kind == FallbackKind::kError ? errors_.at(slot)
                                      : placeholders_.at(slot);
}

auto TextureBinder::Impl::FallbackIndex(const FallbackKind kind,
  const TextureEntry& entry) const -> ShaderVisibleIndex
{
  return Fallback(kind, ShapeOf(entry.texture_type)).srv;
}

auto TextureBinder::Impl::CreateFallback(
  const FallbackKind kind, const FallbackShape shape) -> FallbackBinding
{
  constexpr std::uint32_t kErrorTileSizePx = 8U;
  constexpr std::uint32_t kWhitePixel = 0xFFFFFFFFU;

  const auto desc = MakeFallbackDesc(kind, shape);
  auto texture = gfx_->CreateTexture(desc);
  CHECK_NOTNULL_F(texture, "Failed to create {}", desc.debug_name);

  const auto pixels = kind == FallbackKind::kError
    ? GenerateErrorTextureData(
        Extent(desc.width, desc.height), kErrorTileSizePx)
    : std::vector<std::uint32_t> { kWhitePixel };
  SubmitUniformLayers(texture, std::as_bytes(std::span { pixels }));

  gfx_->GetResourceRegistry().Register(texture);
  const auto srv = RegisterVersion(*texture);
  return FallbackBinding { .texture = std::move(texture), .srv = srv };
}

auto TextureBinder::Impl::RegisterVersion(graphics::Texture& texture)
  -> ShaderVisibleIndex
{
  const auto& desc = texture.GetDescriptor();
  const auto view_desc = MakeTextureSrvViewDesc(desc.format,
    MipRange { .base_mip_level = 0U, .num_mip_levels = desc.mip_levels },
    ArrayRange { .base_array_slice = 0U, .num_array_slices = desc.array_size });

  auto& allocator = gfx_->GetDescriptorAllocator();
  auto handle
    = allocator.AllocateBindless(oxygen::bindless::generated::kTexturesDomain,
      graphics::ResourceViewType::kTexture_SRV);
  if (!handle.IsValid()) {
    throw std::runtime_error(
      "Failed to allocate a texture descriptor for " + desc.debug_name);
  }
  const auto srv
    = ShaderVisibleIndex(allocator.GetShaderVisibleIndex(handle).get());
  // Allocate the revision entry first, so registration is the last step that
  // can fail and a failed call leaves nothing registered.
  auto& revision = content_revisions_[srv];
  gfx_->GetResourceRegistry().RegisterView(
    texture, std::move(handle), view_desc);
  revision = ++next_content_revision_;
  return srv;
}

auto TextureBinder::Impl::Publish(TextureEntry& entry, ShaderVisibleIndex srv)
  -> void
{
  if (entry.srv_index == srv) {
    return;
  }
  entry.srv_index = srv;
  ++resident_content_revision_;
}

auto TextureBinder::Impl::ActivateEntry() -> EntryHandle
{
  std::uint32_t index = next_entry_index_;
  if (free_entry_indices_.empty()) {
    if (free_entry_indices_.capacity() <= index) {
      free_entry_indices_.reserve(
        (std::max)(free_entry_indices_.capacity() * 2U,
          std::size_t { index } + 1U));
    }
    ++next_entry_index_;
  } else {
    index = free_entry_indices_.back();
    free_entry_indices_.pop_back();
  }
  try {
    return entry_reuse_.ActivateSlot(index);
  } catch (...) {
    // Capacity for this push was reserved above or freed by the pop.
    free_entry_indices_.push_back(index);
    throw;
  }
}

auto TextureBinder::Impl::RetireEntryHandle(TextureEntry& entry) noexcept
  -> void
{
  if (auto ticket = entry_reuse_.TryRetire(entry.handle)) {
    const auto result = ticket->Finalize();
    if (result.disposition == nexus::FinalizeDisposition::kReusable
      && result.index.has_value()) {
      // Never reallocates: capacity covers every index ever activated.
      free_entry_indices_.push_back(*result.index);
    }
    ++lifecycle_entry_retirements_;
  }
  entry.handle = {};
}

auto TextureBinder::Impl::IsCurrent(
  const TextureEntry& entry, const EntryHandle handle) const noexcept -> bool
{
  return !entry.evicted && handle.generation.get() != 0U
    && entry.handle == handle;
}

auto TextureBinder::Impl::PublishCompletedUploads() -> void
{
#ifdef OXYGEN_WITH_TRACY
  static const profiling::CpuProfileScopeDesc kScope { .label
    = "Vortex.Texture.PublishCompletedUploads",
    .category = profiling::ProfileCategory::kUpload };
  const profiling::CpuProfileScope profile(kScope);
#endif
  // Completed uploads are published on the render thread, after the
  // UploadCoordinator reports their completion. Each version is published
  // through a new descriptor; nothing an in-flight frame reads is rewritten.
  if (!uploader_) {
    return;
  }

  const auto visits = std::min(
    pending_completions_.size(), limits_.max_completion_visits_per_frame);
  for (std::size_t visit = 0; visit < visits; ++visit) {
    const auto work = pending_completions_.begin();
    bool handled = false;
    const ScopeGuard remove_completed([&] noexcept -> void {
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
      || !IsCurrent(entry, work->handle)) {
      ++lifecycle_stale_discard_count_;
      handled = true;
      continue;
    }
    if (entry.eviction_pending) {
      pending_completions_.splice(
        pending_completions_.end(), pending_completions_, work);
      continue;
    }
    const auto maybe_result = entry.pending_ticket->TryGetResult();
    if (!maybe_result.has_value()) {
      pending_completions_.splice(
        pending_completions_.end(), pending_completions_, work);
      continue;
    }
    DLOG_F(2, "Upload ticket {} completed for resource key {} (success={})",
      work->ticket_id, resource_key, maybe_result->success);
    if (!maybe_result->success) {
      LOG_F(WARNING,
        "Texture upload failed for {} (ticket={}): keeping the placeholder",
        resource_key, work->ticket_id);
      handled = true;
      HandleLoadFailure(
        resource_key, entry, FailurePolicy::kKeepPlaceholderBound);
      continue;
    }
    // A pending upload exists only while no version is resident.
    DCHECK_F(!entry.texture);
    // Throws without side effects; the completion stays queued and publishes
    // on a later frame.
    const auto srv = RegisterVersion(*entry.pending_texture);
    handled = true;
    entry.pending_ticket.reset();
    entry.texture = std::move(entry.pending_texture);
    entry.load_failed = false;
    Publish(entry, srv);
  }
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
      total_bytes = std::numeric_limits<std::size_t>::max();
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
 Initiates asynchronous loading of a texture resource. The entry keeps
 publishing its fallback until the upload completes.

 @param resource_key Opaque ResourceKey identifying the resource to load
 @param entry Entry whose current identity the completion must match
*/
auto TextureBinder::Impl::InitiateAsyncLoad(content::ResourceKey resource_key,
  const TextureEntry& entry, const char* reason) -> void
{
  LOG_F(INFO,
    "Initiating async load for resource key: {} (reason={}, entry={}, "
    "pending={}, last_eviction_reason={})",
    resource_key, reason, entry.handle.index, entry.pending_ticket.has_value(),
    entry.last_eviction_reason);

  texture_loader_->StartLoadTexture(resource_key,
    [gate = callback_gate_, this, resource_key, handle = entry.handle](
      // NOLINTNEXTLINE(*-unnecessary-value-param)
      std::shared_ptr<data::TextureResource> tex_res) -> void {
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

 This callback may run off the render thread. It only queues the decoded
 payload; `SubmitQueuedTextureUploads()` validates the entry identity and
 submits the upload on the render thread.

 @param resource_key Opaque key for the entry being updated.
 @param handle Entry identity captured when the load request was issued.
 @param tex_res Loaded texture resource, or `nullptr` on load failure.
*/
auto TextureBinder::Impl::OnTextureResourceLoaded(
  const content::ResourceKey resource_key, const EntryHandle handle,
  // NOLINTNEXTLINE(*-unnecessary-value-param) - moved from a lambda capture
  std::shared_ptr<data::TextureResource> tex_res) -> void
{
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
      "exceed {} bytes",
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
      if (entry == texture_map_.end() || entry->second.eviction_pending
        || !IsCurrent(entry->second, work->handle)) {
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
  DCHECK_NOTNULL_F(uploader_, "UploadCoordinator cannot be null");
  DCHECK_NOTNULL_F(staging_provider_, "StagingProvider cannot be null");

  std::size_t submitted_bytes = 0U;

  for (std::size_t visit = 0; visit < limits_.max_upload_visits_per_frame;
    ++visit) {
    if (submitted_bytes >= max_bytes) {
      return;
    }

    PendingUpload pending;
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
        pending_upload_bytes_
          -= (std::min)(pending_upload_bytes_, pending.resource->GetDataSize());
      }
    }

    TextureEntry* entry_ptr = this->FindEntryOrLog(pending.key);
    if (entry_ptr == nullptr) {
      continue;
    }
    auto& entry = *entry_ptr;

    if (entry.eviction_pending || !IsCurrent(entry, pending.handle)) {
      ++lifecycle_stale_discard_count_;
      DLOG_F(4, "Discarding pending upload for {} due to eviction or reload",
        pending.key);
      continue;
    }

    if (!pending.resource) {
      LOG_F(WARNING, "Async texture load returned null for resource {}",
        pending.key);
      this->HandleLoadFailure(
        pending.key, entry, FailurePolicy::kBindErrorTexture);
      continue;
    }

    entry.texture_type = pending.resource->GetTextureType();
    if (!entry.texture) {
      Publish(entry, FallbackIndex(FallbackKind::kPlaceholder, entry));
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
        pending.key, entry, FailurePolicy::kBindErrorTexture);
      continue;
    }

    auto prepared = std::get<PreparedTexture2DUpload>(prepared_result);
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
    if (entry.eviction_pending || !IsCurrent(entry, retry.handle)
      || entry.pending_ticket.has_value()) {
      continue;
    }

    ++lifecycle_async_retries_;
    InitiateAsyncLoad(retry.key, entry, "deferred_backlog_retry");
  }
}

//=== Eviction handling =====================================================//

/*!
 Accept eviction requests queued by the asset loader. Accepted entries stop
 reporting ready once no lease holds them.
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
    it->handle = entry->second.handle;
    entry->second.eviction_pending = true;
    ++it;
  }
}

/*!
 Evict accepted entries that no lease holds.

 The entry publishes the shared placeholder of its shape. Its resident and
 in-flight textures retire with their descriptors once the frames that may
 read them complete, and its identity retires so late completions for the
 old load are discarded.
*/
auto TextureBinder::Impl::ProcessEvictions() -> void
{
  AcceptEvictions();
#ifdef OXYGEN_WITH_TRACY
  static const profiling::CpuProfileScopeDesc kScope { .label
    = "Vortex.Texture.ProcessEvictions",
    .category = profiling::ProfileCategory::kUpload };
  const profiling::CpuProfileScope profile(kScope);
#endif
  const auto visits
    = std::min(eviction_work_.size(), limits_.max_eviction_visits_per_frame);
  for (std::size_t visit = 0; visit < visits; ++visit) {
    const auto work = eviction_work_.begin();
    const auto found = texture_map_.find(work->key);
    if (found == texture_map_.end()
      || !IsCurrent(found->second, work->handle)) {
      eviction_work_.erase(work);
      continue;
    }
    auto& entry = found->second;
    if (!entry.resident_lease.expired()) {
      eviction_work_.splice(eviction_work_.end(), eviction_work_, work);
      continue;
    }

    gfx_->RetireResource(entry.texture);
    gfx_->RetireResource(entry.pending_texture);
    RetireEntryHandle(entry);
    entry.pending_ticket.reset();
    entry.evicted = true;
    entry.eviction_pending = false;
    entry.load_failed = false;
    entry.last_eviction_reason = work->reason;
    Publish(entry, FallbackIndex(FallbackKind::kPlaceholder, entry));
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
    PendingCompletion {
      .key = resource_key,
      .handle = entry.handle,
      .ticket_id = vortex::upload::TicketId { 0 },
    });
  bool tracked = false;
  const ScopeGuard rollback([&] noexcept -> void {
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
    gfx_->RetireResource(new_texture);
    this->HandleLoadFailure(
      resource_key, entry, FailurePolicy::kKeepPlaceholderBound);
    return;
  }

  ++total_upload_submissions_;

  DLOG_F(3, "ticket: {}", upload_result->Id());

  // Registered now so that retirement releases it on every path; its view is
  // registered when the upload completes.
  gfx_->GetResourceRegistry().Register(new_texture);

  work->ticket_id = upload_result->Id();
  entry.pending_ticket = *upload_result;
  entry.pending_texture = std::move(new_texture);
  entry.load_failed = false;
  tracked = true;
  DLOG_F(3, "submitted upload ticket {} for resource {}",
    entry.pending_ticket->Id(), resource_key);
}

/*!
 Apply a load or upload failure policy to an entry.

 - `FailurePolicy::kBindErrorTexture`: publish the shared error binding of the
   entry's shape.
 - `FailurePolicy::kKeepPlaceholderBound`: keep publishing the placeholder;
   used when the upload itself failed.

 Either way the in-flight destination retires, and the entry reports failure
 until it is evicted and reloaded.

 @param resource_key Opaque key for the failing entry.
 @param entry Entry to update.
 @param policy Failure policy to apply.
*/
auto TextureBinder::Impl::HandleLoadFailure(
  const content::ResourceKey resource_key, TextureEntry& entry,
  const FailurePolicy policy) -> void
{
  DLOG_SCOPE_F(3, "TextureBinder HandleLoadFailure");
  DLOG_F(3, "resource: {}", resource_key);
  DLOG_F(3, "policy: {}",
    (policy == FailurePolicy::kBindErrorTexture) ? "BindErrorTexture"
                                                 : "KeepPlaceholderBound");

  ++load_failures_;
  entry.load_failed = true;
  entry.pending_ticket.reset();
  gfx_->RetireResource(entry.pending_texture);

  if (policy == FailurePolicy::kBindErrorTexture) {
    gfx_->RetireResource(entry.texture);
    Publish(entry, FallbackIndex(FallbackKind::kError, entry));
    LOG_F(INFO, "Published the error texture for resource {}", resource_key);
  }
}

auto TextureBinder::Impl::SubmitUniformLayers(
  const std::shared_ptr<graphics::Texture>& texture,
  const std::span<const std::byte> layer_bytes) -> void
{
  if (!uploader_ || !staging_provider_) {
    return;
  }

  const auto& desc = texture->GetDescriptor();
  const auto& format_info = graphics::detail::GetFormatInfo(desc.format);
  const auto bytes_per_row = desc.width * format_info.bytes_per_block;
  CHECK_EQ_F(layer_bytes.size(),
    static_cast<std::size_t>(bytes_per_row) * desc.height,
    "Fallback texture data does not match {}", desc.debug_name);

  vortex::upload::UploadRequest req {
    .kind = vortex::upload::UploadKind::kTexture2D,
    .debug_name = desc.debug_name,
    .desc = vortex::upload::UploadTextureDesc {
      .dst = texture,
      .width = desc.width,
      .height = desc.height,
      .depth = desc.depth,
      .format = desc.format,
    },
    .subresources = {},
    .data = {},
  };
  vortex::upload::UploadTextureSourceView src_view;
  for (std::uint32_t layer = 0U; layer < desc.array_size; ++layer) {
    req.subresources.push_back(vortex::upload::UploadSubresource {
      .mip = 0U,
      .array_slice = layer,
      .x = 0U,
      .y = 0U,
      .z = 0U,
      .width = 0U,
      .height = 0U,
      .depth = 1U,
    });
    src_view.subresources.push_back({
      .bytes = layer_bytes,
      .row_pitch = bytes_per_row,
      .slice_pitch = bytes_per_row * desc.height,
    });
  }
  req.data = std::move(src_view);

  const auto result = uploader_->Submit(req, *staging_provider_);
  if (!result) {
    const auto error_code
      = oxygen::vortex::upload::make_error_code(result.error());
    LOG_F(ERROR, "Texture upload failed ({}): {}", desc.debug_name,
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
