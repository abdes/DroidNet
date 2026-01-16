//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <cstring>
#include <optional>
#include <string_view>

#include <Oxygen/Base/Logging.h>
#include <Oxygen/Content/Import/Async/Pipelines/TexturePipeline.h>
#include <Oxygen/Content/Import/TextureCooker.h>
#include <Oxygen/Content/Import/TextureImportError.h>
#include <Oxygen/Content/Import/emit/TextureEmissionUtils.h>
#include <Oxygen/Data/PakFormat.h>

namespace oxygen::content::import {

namespace {

  [[nodiscard]] auto MakeErrorDiagnostic(
    TextureImportError error, std::string_view source_id) -> ImportDiagnostic
  {
    ImportDiagnostic diag {
      .severity = ImportSeverity::kError,
      .code = "texture.cook_failed",
      .message = std::string("Texture cook failed: ") + to_string(error) + " ("
        + std::string(source_id) + ")",
      .source_path = std::string(source_id),
      .object_path = {},
    };
    return diag;
  }

  [[nodiscard]] auto IsKnownPackingPolicyId(std::string_view policy_id) -> bool
  {
    return policy_id == "d3d12" || policy_id == "tight";
  }

  [[nodiscard]] auto MakePackingPolicyDiagnostic(std::string_view policy_id,
    std::string_view fallback_id, std::string_view source_id)
    -> ImportDiagnostic
  {
    ImportDiagnostic diag {
      .severity = ImportSeverity::kWarning,
      .code = "texture.packing_policy_unknown",
      .message = std::string("Unknown packing policy '")
        + std::string(policy_id) + "'; using '" + std::string(fallback_id)
        + "'.",
      .source_path = std::string(source_id),
      .object_path = {},
    };
    return diag;
  }

  [[nodiscard]] auto ParseLayouts(std::span<const std::byte> payload)
    -> std::vector<data::pak::SubresourceLayout>
  {
    if (payload.size() < sizeof(data::pak::TexturePayloadHeader)) {
      return {};
    }

    data::pak::TexturePayloadHeader header {};
    std::memcpy(&header, payload.data(), sizeof(header));
    const auto count = header.subresource_count;
    const auto layouts_offset = header.layouts_offset_bytes;
    const auto layouts_size
      = static_cast<size_t>(count) * sizeof(data::pak::SubresourceLayout);
    if (layouts_offset + layouts_size > payload.size()) {
      return {};
    }

    std::vector<data::pak::SubresourceLayout> layouts(count);
    std::memcpy(layouts.data(), payload.data() + layouts_offset, layouts_size);
    return layouts;
  }

  [[nodiscard]] auto BuildPlaceholderPayload(std::string_view texture_id,
    std::string_view packing_policy_id) -> CookedTexturePayload
  {
    emit::CookerConfig config {};
    config.packing_policy_id = std::string(packing_policy_id);

    auto placeholder
      = emit::CreatePlaceholderForMissingTexture(texture_id, config);

    CookedTexturePayload cooked {};
    cooked.payload = std::move(placeholder.payload);
    cooked.layouts = ParseLayouts(cooked.payload);
    cooked.desc.texture_type = TextureType::kTexture2D;
    cooked.desc.width = 1;
    cooked.desc.height = 1;
    cooked.desc.depth = 1;
    cooked.desc.array_layers = 1;
    cooked.desc.mip_levels = 1;
    cooked.desc.format = Format::kRGBA8UNorm;
    cooked.desc.packing_policy_id = std::string(packing_policy_id);

    if (cooked.payload.size() >= sizeof(data::pak::TexturePayloadHeader)) {
      data::pak::TexturePayloadHeader header {};
      std::memcpy(&header, cooked.payload.data(), sizeof(header));
      cooked.desc.content_hash = header.content_hash;
    }

    return cooked;
  }

  [[nodiscard]] auto CookFromSourceContent(
    TexturePipeline::SourceContent source, TextureImportDesc desc,
    const ITexturePackingPolicy& policy, const bool output_format_is_override)
    -> oxygen::Result<CookedTexturePayload, TextureImportError>
  {
    return std::visit(
      [&](auto&& value)
        -> oxygen::Result<CookedTexturePayload, TextureImportError> {
        using ValueT = std::decay_t<decltype(value)>;

        if constexpr (std::is_same_v<ValueT, TexturePipeline::SourceBytes>) {
          if (value.bytes.empty()) {
            return ::oxygen::Err(TextureImportError::kFileNotFound);
          }

          if (!output_format_is_override) {
            auto decoded = detail::DecodeSource(value.bytes, desc);
            if (!decoded) {
              return ::oxygen::Err(decoded.error());
            }

            const auto& meta = decoded->Meta();
            desc.output_format = meta.format;
            desc.bc7_quality = Bc7Quality::kNone;
            return CookTexture(std::move(*decoded), desc, policy);
          }

          return CookTexture(value.bytes, desc, policy);
        } else if constexpr (std::is_same_v<ValueT, TextureSourceSet>) {
          if (value.IsEmpty()) {
            return ::oxygen::Err(TextureImportError::kFileNotFound);
          }

          std::vector<ScratchImage> decoded_images;
          decoded_images.reserve(value.Count());
          std::vector<uint16_t> array_layers;
          array_layers.reserve(value.Count());

          for (const auto& source : value.Sources()) {
            if (source.bytes.empty()) {
              return ::oxygen::Err(TextureImportError::kFileNotFound);
            }

            auto per_source_desc = desc;
            per_source_desc.source_id = source.source_id;
            auto decoded = detail::DecodeSource(source.bytes, per_source_desc);
            if (!decoded) {
              return ::oxygen::Err(decoded.error());
            }

            decoded_images.push_back(std::move(*decoded));
            array_layers.push_back(source.subresource.array_layer);
          }

          const auto& first_meta = decoded_images[0].Meta();
          for (size_t i = 1; i < decoded_images.size(); ++i) {
            const auto& meta = decoded_images[i].Meta();
            if (meta.width != first_meta.width
              || meta.height != first_meta.height) {
              return ::oxygen::Err(TextureImportError::kDimensionMismatch);
            }
            if (meta.format != first_meta.format) {
              return ::oxygen::Err(TextureImportError::kOutputFormatInvalid);
            }
          }

          if (!output_format_is_override) {
            desc.output_format = first_meta.format;
            desc.bc7_quality = Bc7Quality::kNone;
          }

          if (desc.texture_type == TextureType::kTextureCube) {
            std::array<ScratchImage, kCubeFaceCount> faces;
            std::array<bool, kCubeFaceCount> filled {};
            for (size_t i = 0; i < decoded_images.size(); ++i) {
              const auto face_idx = array_layers[i];
              if (face_idx >= kCubeFaceCount || filled[face_idx]) {
                return ::oxygen::Err(
                  TextureImportError::kArrayLayerCountInvalid);
              }
              faces[face_idx] = std::move(decoded_images[i]);
              filled[face_idx] = true;
            }

            for (const auto present : filled) {
              if (!present) {
                return ::oxygen::Err(
                  TextureImportError::kArrayLayerCountInvalid);
              }
            }

            auto cube = AssembleCubeFromFaces(
              std::span<const ScratchImage, kCubeFaceCount>(faces));
            if (!cube) {
              return ::oxygen::Err(cube.error());
            }

            return CookTexture(std::move(*cube), desc, policy);
          }

          return ::oxygen::Err(TextureImportError::kUnsupportedFormat);
        } else {
          if (!output_format_is_override) {
            const auto& meta = value.Meta();
            desc.output_format = meta.format;
            desc.bc7_quality = Bc7Quality::kNone;
          }

          return CookTexture(std::move(value), desc, policy);
        }
      },
      std::move(source));
  }

} // namespace

TexturePipeline::TexturePipeline(co::ThreadPool& thread_pool, Config config)
  : thread_pool_(thread_pool)
  , config_(config)
  , input_channel_(config.queue_capacity)
  , output_channel_(config.queue_capacity)
{
}

TexturePipeline::~TexturePipeline()
{
  if (started_) {
    DLOG_IF_F(WARNING, HasPending(),
      "TexturePipeline destroyed with {} pending items", PendingCount());
  }

  input_channel_.Close();
  output_channel_.Close();
}

auto TexturePipeline::Start(co::Nursery& nursery) -> void
{
  DCHECK_F(!started_, "TexturePipeline::Start() called more than once");
  started_ = true;

  const auto worker_count = std::max(1U, config_.worker_count);
  for (uint32_t i = 0; i < worker_count; ++i) {
    nursery.Start([this]() -> co::Co<> { co_await Worker(); });
  }
}

auto TexturePipeline::Submit(WorkItem item) -> co::Co<>
{
  ++pending_;
  submitted_.fetch_add(1, std::memory_order_acq_rel);
  co_await input_channel_.Send(std::move(item));
}

auto TexturePipeline::TrySubmit(WorkItem item) -> bool
{
  if (input_channel_.Closed()) {
    return false;
  }

  if (input_channel_.Full()) {
    return false;
  }

  const auto ok = input_channel_.TrySend(std::move(item));
  if (ok) {
    ++pending_;
    submitted_.fetch_add(1, std::memory_order_acq_rel);
  }
  return ok;
}

auto TexturePipeline::Collect() -> co::Co<WorkResult>
{
  auto maybe_result = co_await output_channel_.Receive();
  if (!maybe_result.has_value()) {
    co_return WorkResult {
      .source_id = {},
      .texture_id = {},
      .source_key = nullptr,
      .cooked = {},
      .used_placeholder = false,
      .diagnostics = {},
      .success = false,
    };
  }

  pending_.fetch_sub(1, std::memory_order_acq_rel);
  if (maybe_result->success) {
    completed_.fetch_add(1, std::memory_order_acq_rel);
  } else {
    failed_.fetch_add(1, std::memory_order_acq_rel);
  }
  co_return std::move(*maybe_result);
}

auto TexturePipeline::Close() -> void { input_channel_.Close(); }

auto TexturePipeline::HasPending() const noexcept -> bool
{
  return pending_.load(std::memory_order_acquire) > 0;
}

auto TexturePipeline::PendingCount() const noexcept -> size_t
{
  return pending_.load(std::memory_order_acquire);
}

auto TexturePipeline::GetProgress() const noexcept -> PipelineProgress
{
  const auto submitted = submitted_.load(std::memory_order_acquire);
  const auto completed = completed_.load(std::memory_order_acquire);
  const auto failed = failed_.load(std::memory_order_acquire);
  return PipelineProgress {
    .submitted = submitted,
    .completed = completed,
    .failed = failed,
    .in_flight = submitted - completed - failed,
    .throughput = 0.0F,
  };
}

auto TexturePipeline::Worker() -> co::Co<>
{
  while (true) {
    auto maybe_item = co_await input_channel_.Receive();
    if (!maybe_item.has_value()) {
      break;
    }

    auto item = std::move(*maybe_item);
    if (item.stop_token.stop_requested()) {
      co_await ReportCancelled(std::move(item));
      continue;
    }

    const auto& policy = emit::GetPackingPolicy(item.packing_policy_id);
    const bool unknown_policy = !IsKnownPackingPolicyId(item.packing_policy_id);
    auto local_desc = item.desc;
    local_desc.source_id = item.source_id;
    local_desc.stop_token = item.stop_token;

    auto source_ptr = std::make_shared<SourceContent>(std::move(item.source));
    auto result = co_await thread_pool_.Run(
      [source_ptr, desc = std::move(local_desc), &policy,
        output_format_is_override = item.output_format_is_override,
        stop_token = item.stop_token](co::ThreadPool::CancelToken cancelled)
        -> oxygen::Result<CookedTexturePayload, TextureImportError> {
        if (stop_token.stop_requested() || cancelled) {
          return ::oxygen::Err(TextureImportError::kCancelled);
        }
        return CookFromSourceContent(
          std::move(*source_ptr), desc, policy, output_format_is_override);
      });

    WorkResult output {
      .source_id = std::move(item.source_id),
      .texture_id = std::move(item.texture_id),
      .source_key = item.source_key,
      .cooked = std::nullopt,
      .used_placeholder = false,
      .diagnostics = {},
      .success = false,
    };

    if (unknown_policy) {
      output.diagnostics.push_back(MakePackingPolicyDiagnostic(
        item.packing_policy_id, policy.Id(), output.source_id));
    }

    if (result.has_value()) {
      output.cooked = std::move(result.value());
      output.success = true;
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    const auto error = result.error();
    if (error == TextureImportError::kCancelled) {
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    if (item.failure_policy == FailurePolicy::kPlaceholder) {
      output.cooked
        = BuildPlaceholderPayload(output.texture_id, item.packing_policy_id);
      output.used_placeholder = true;
      output.success = true;
      co_await output_channel_.Send(std::move(output));
      continue;
    }

    output.diagnostics.push_back(MakeErrorDiagnostic(error, output.source_id));
    co_await output_channel_.Send(std::move(output));
  }

  co_return;
}

auto TexturePipeline::ReportCancelled(WorkItem item) -> co::Co<>
{
  WorkResult cancelled {
    .source_id = std::move(item.source_id),
    .texture_id = std::move(item.texture_id),
    .source_key = item.source_key,
    .cooked = std::nullopt,
    .used_placeholder = false,
    .diagnostics = {},
    .success = false,
  };
  co_await output_channel_.Send(std::move(cancelled));
}

} // namespace oxygen::content::import
