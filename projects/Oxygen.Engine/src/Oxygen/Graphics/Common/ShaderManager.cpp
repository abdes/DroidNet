//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/ShaderManager.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <unordered_set>

#include <Oxygen/Base/Hash.h>
#include <Oxygen/Base/Logging.h>
#include <Oxygen/Graphics/Common/ShaderByteCode.h>
#include <Oxygen/Graphics/Common/ShaderCompiler.h>
#include <Oxygen/Graphics/Common/Shaders.h>
#include <Oxygen/Serio/FileStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

using oxygen::graphics::CompiledShaderInfo;
using oxygen::graphics::ShaderManager;

namespace {

auto GetEnvVar(const char* name) -> std::optional<std::string>
{
#ifdef _MSC_VER
  char* value = nullptr;
  size_t len = 0;
  if (_dupenv_s(&value, &len, name) != 0 || value == nullptr) {
    return std::nullopt;
  }
  std::unique_ptr<char, decltype(&std::free)> holder(value, &std::free);
  return std::string(holder.get());
#else
  const char* value = std::getenv(name);
  if (value == nullptr) {
    return std::nullopt;
  }
  return std::string(value);
#endif
}

auto ReadFileContent(const std::filesystem::path& file_path)
  -> std::optional<std::string>
{
  std::ifstream file(file_path, std::ios::binary);
  if (!file) {
    return std::nullopt;
  }

  std::string content(
    (std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
  return content;
}

auto ExtractQuotedIncludes(std::string_view source) -> std::vector<std::string>
{
  std::vector<std::string> includes;

  size_t pos = 0;
  while (pos < source.size()) {
    const size_t line_end = source.find_first_of("\r\n", pos);
    const auto line = source.substr(pos,
      (line_end == std::string_view::npos) ? source.size() - pos
                                           : line_end - pos);

    const size_t inc = line.find("#include");
    if (inc != std::string_view::npos) {
      const size_t first_quote = line.find('"', inc);
      if (first_quote != std::string_view::npos) {
        const size_t second_quote = line.find('"', first_quote + 1);
        if (second_quote != std::string_view::npos
          && second_quote > first_quote + 1) {
          includes.emplace_back(
            line.substr(first_quote + 1, second_quote - first_quote - 1));
        }
      }
    }

    if (line_end == std::string_view::npos) {
      break;
    }
    pos = source.find_first_not_of("\r\n", line_end);
    if (pos == std::string_view::npos) {
      break;
    }
  }

  return includes;
}

auto ResolveIncludePath(const std::filesystem::path& including_file,
  std::string_view include_name,
  const std::vector<std::filesystem::path>& include_dirs)
  -> std::optional<std::filesystem::path>
{
  const std::filesystem::path include_rel(include_name);

  {
    auto candidate = including_file.parent_path() / include_rel;
    if (exists(candidate)) {
      return candidate;
    }
  }

  for (const auto& dir : include_dirs) {
    auto candidate = dir / include_rel;
    if (exists(candidate)) {
      return candidate;
    }
  }

  return std::nullopt;
}

auto ComputeSourceHashWithIncludes(const std::filesystem::path& source_path,
  const std::vector<std::filesystem::path>& include_dirs) -> uint64_t
{
  std::unordered_set<std::string> visited;
  size_t seed = 0;

  std::vector<std::filesystem::path> stack;
  stack.emplace_back(source_path);

  while (!stack.empty()) {
    auto current = std::move(stack.back());
    stack.pop_back();

    std::error_code ec;
    const auto canonical = weakly_canonical(current, ec);
    const std::string key
      = (ec ? current.lexically_normal() : canonical).string();
    if (!visited.insert(key).second) {
      continue;
    }

    const auto content_opt = ReadFileContent(current);
    if (!content_opt.has_value()) {
      oxygen::HashCombine(seed, key);
      oxygen::HashCombine(seed, uint64_t { 0 });
      continue;
    }

    const auto& content = *content_opt;
    const auto content_hash
      = oxygen::ComputeFNV1a64(content.data(), content.size());
    oxygen::HashCombine(seed, key);
    oxygen::HashCombine(seed, content_hash);

    const auto includes = ExtractQuotedIncludes(content);
    for (const auto& inc : includes) {
      if (auto resolved = ResolveIncludePath(current, inc, include_dirs);
        resolved.has_value()) {
        stack.emplace_back(std::move(*resolved));
      } else {
        oxygen::HashCombine(seed, inc);
      }
    }
  }

  return static_cast<uint64_t>(seed);
}

auto IsSourceFileNewer(const std::filesystem::path& source_path,
  const std::chrono::system_clock::time_point compile_time) -> bool
{
  if (!exists(source_path)) {
    return true;
  }

  const auto file_time = last_write_time(source_path);
  const auto compile_time_tp = compile_time;

  // Convert file_time to system_clock::time_point
  const auto file_time_tp
    = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      file_time - std::filesystem::file_time_type::clock::now()
      + std::chrono::system_clock::now());

  return file_time_tp > compile_time_tp;
}

} // namespace

namespace {
constexpr uint32_t kArchiveMagic = 0x4F585348; // "OXSH"
constexpr uint32_t kArchiveVersion = 2;
} // namespace

struct ArchiveHeader {
  uint32_t magic;
  uint32_t version;
  size_t shader_count;
};

namespace oxygen::serio {

//! Store specialization for ArchiveHeader.
inline auto Store(AnyWriter& writer, const ArchiveHeader& header)
  -> Result<void>
{
  CHECK_RESULT(writer.Write(header.magic));
  CHECK_RESULT(writer.Write(header.version));
  CHECK_RESULT(writer.Write(header.shader_count));
  return {};
}

//! Store specialization for ArchiveHeader.
inline auto Load(AnyReader& reader, ArchiveHeader& header) -> Result<void>
{
  CHECK_RESULT(reader.ReadInto(header.magic));
  CHECK_RESULT(reader.ReadInto(header.version));
  CHECK_RESULT(reader.ReadInto(header.shader_count));
  return {};
}

//! Store specialization for ArchiveHeader.
inline auto Store(AnyWriter& writer, const ShaderType& value) -> Result<void>
{
  CHECK_RESULT(
    writer.Write(static_cast<std::underlying_type_t<ShaderType>>(value)));
  return {};
}

//! Store specialization for ArchiveHeader.
inline auto Load(AnyReader& reader, ShaderType& value) -> Result<void>
{
  std::underlying_type_t<ShaderType> shader_type_value = 0;
  CHECK_RESULT(reader.ReadInto(shader_type_value));
  if (shader_type_value
      < static_cast<std::underlying_type_t<ShaderType>>(ShaderType::kUnknown)
    || shader_type_value > static_cast<std::underlying_type_t<ShaderType>>(
         ShaderType::kMaxShaderType)) {
    return std::make_error_code(std::errc::invalid_argument);
  }
  value = static_cast<ShaderType>(shader_type_value);
  return {};
}

} // namespace oxygen::serio

namespace {
auto GetArchivePath(const ShaderManager::Config& config)
  -> std::filesystem::path
{
  std::filesystem::path archive_path = config.archive_dir;
  // Ensure the archive directory exists
  try {
    create_directories(archive_path);
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_F(ERROR, "Failed to create archive directory `{}`: {}",
      archive_path.string(), e.what());
    throw;
  }

  archive_path /= config.archive_file_name;
  LOG_F(INFO, "Using archive file at: {}", archive_path.string());

  return archive_path;
}

} // namespace

auto ShaderManager::Initialize() -> void
{
  DCHECK_NOTNULL_F(config_.compiler, "Shader compiler not set.");
  DCHECK_F(!config_.shaders.empty(), "No shaders specified.");
  DCHECK_F(!config_.source_dir.empty(), "No shader source directory specified");

  shader_infos_.assign(config_.shaders.begin(), config_.shaders.end());

  archive_path_ = GetArchivePath(config_);

  if (exists(archive_path_)) {
    Load();
  }

  UpdateOutdatedShaders();
}

auto ShaderManager::AddCompiledShader(CompiledShader shader) -> bool
{
  if (!shader.bytecode || shader.bytecode->Data() == nullptr
    || shader.bytecode->Size() == 0) {
    return false;
  }

  if (shader.info.cache_key == 0) {
    return false;
  }

  shader_cache_[shader.info.cache_key] = std::move(shader);
  return true;
}

auto ShaderManager::GetShaderBytecode(const ShaderRequest& request) const
  -> std::shared_ptr<IShaderByteCode>
{
  const auto canonical = CanonicalizeShaderRequest(ShaderRequest(request));
  const auto key = ComputeShaderRequestKey(canonical);
  const auto it = shader_cache_.find(key);
  if (it == shader_cache_.end()) {
    return nullptr;
  }

  return it->second.bytecode;
}

auto ShaderManager::IsShaderOutdated(const ShaderInfo& shader) const -> bool
{
  const auto canonical = CanonicalizeShaderRequest(ShaderRequest {
    .stage = shader.type,
    .source_path = shader.relative_path,
    .entry_point = shader.entry_point,
    .defines = {},
  });
  const auto key = ComputeShaderRequestKey(canonical);

  const auto it = shader_cache_.find(key);
  if (it == shader_cache_.end()) {
    return true;
  }

  // Check file exists and hash matches
  const auto& info = it->second.info;
  std::vector<std::filesystem::path> include_dirs;

  include_dirs.emplace_back(config_.source_dir);
  for (const auto& dir : config_.include_dirs) {
    include_dirs.emplace_back(dir);
  }

  const std::filesystem::path source_path
    = config_.source_dir / std::filesystem::path(info.request.source_path);

  if (const auto current_hash
    = ComputeSourceHashWithIncludes(source_path, include_dirs);
    current_hash != info.source_hash) {
    return true;
  }

  return IsSourceFileNewer(source_path, info.compile_time);
}

auto ShaderManager::GetOutdatedShaders() const -> std::vector<ShaderInfo>
{
  std::vector<ShaderInfo> outdated;
  for (const auto& profile : shader_infos_) {
    if (IsShaderOutdated(profile)) {
      outdated.push_back(profile);
    }
  }
  return outdated;
}

void ShaderManager::UpdateOutdatedShaders()
{
  auto outdated = GetOutdatedShaders();
  if (outdated.empty()) {
    LOG_F(INFO, "All {} shaders are up-to-date.", shader_infos_.size());
    return;
  }

  const auto result
    = std::ranges::all_of(outdated, [this](const auto& profile) -> auto {
        return CompileAndAddShader(profile);
      });

  if (result) {
    LOG_F(
      INFO, "All {} outdated shaders have been recompiled.", outdated.size());
    Save();
  } else {
    LOG_F(WARNING,
      "Some outdated shaders were not successfully recompiled; not saving the "
      "shaders archive.");
  }
}

auto ShaderManager::RecompileAll() -> bool
{
  shader_cache_.clear();
  return std::ranges::all_of(
    shader_infos_, [this](const auto& profile) -> auto {
      return CompileAndAddShader(profile);
    });
}

namespace {
auto GetCompileTime() -> int64_t
{
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}
}

auto ShaderManager::Save() const -> void
{
  serio::FileStream stream(archive_path_, std::ios::out);
  serio::Writer writer(stream);

  ArchiveHeader header {
    .magic = kArchiveMagic,
    .version = kArchiveVersion,
    .shader_count = shader_cache_.size(),
  };
  if (auto result = writer.Write(header); !result.has_value()) {
    throw std::runtime_error("archive saving error: header");
  }

  for (const auto& [info, bytecode] : shader_cache_ | std::views::values) {
    const auto stage
      = static_cast<std::underlying_type_t<ShaderType>>(info.request.stage);
    if (auto result = writer.Write(stage); !result.has_value()) {
      throw std::runtime_error("archive saving error: shader type");
    }

    if (auto result = writer.Write(info.cache_key); !result.has_value()) {
      throw std::runtime_error("archive saving error: shader cache key");
    }

    if (auto result = writer.Write<std::string>(info.request.source_path);
      !result.has_value()) {
      throw std::runtime_error("archive saving error: source file path");
    }

    if (auto result = writer.Write<std::string>(info.request.entry_point);
      !result.has_value()) {
      throw std::runtime_error("archive saving error: entry point");
    }

    if (auto result = writer.Write(info.request.defines.size());
      !result.has_value()) {
      throw std::runtime_error("archive saving error: defines count");
    }
    for (const auto& def : info.request.defines) {
      if (auto result = writer.Write<std::string>(def.name);
        !result.has_value()) {
        throw std::runtime_error("archive saving error: define name");
      }

      const uint8_t has_value = def.value.has_value() ? uint8_t { 1 } : 0;
      if (auto result = writer.Write(has_value); !result.has_value()) {
        throw std::runtime_error("archive saving error: define has value");
      }
      if (has_value != 0) {
        if (auto result = writer.Write<std::string>(*def.value);
          !result.has_value()) {
          throw std::runtime_error("archive saving error: define value");
        }
      }
    }

    if (auto result = writer.Write(info.source_hash); !result.has_value()) {
      throw std::runtime_error("archive saving error: source hash");
    }

    auto compile_time_ms = GetCompileTime();
    if (auto result = writer.Write(compile_time_ms); !result.has_value()) {
      throw std::runtime_error("archive saving error: compile time");
    }

    if (auto result = writer.Write(info.compiled_bloc_size);
      !result.has_value()) {
      throw std::runtime_error("archive saving error: compiled bloc size");
    }

    // Write bytecode data as array
    auto result = writer.Write(
      std::span(bytecode->Data(), bytecode->Size() / sizeof(uint32_t)));
    if (!result.has_value()) {
      throw std::runtime_error("archive saving error: bytecode");
    }
  }

  LOG_F(INFO, "Shaders archive saved to: {}", archive_path_.string());
}

void ShaderManager::Load()
{
  serio::FileStream stream(archive_path_, std::ios::in);
  serio::Reader reader(stream);

  auto header = reader.Read<ArchiveHeader>();
  if (!header.has_value()) {
    LOG_F(WARNING, "Shader archive read failed ({}); discarding archive at: {}",
      header.error().message(), archive_path_.string());
    shader_cache_.clear();
    std::error_code ec;
    std::filesystem::remove(archive_path_, ec);
    return;
  }

  if (header.value().magic != kArchiveMagic
    || header.value().version != kArchiveVersion) {
    LOG_F(WARNING,
      "Shader archive header mismatch (expected magic=0x{:08x} version={}, "
      "got magic=0x{:08x} version={}); discarding archive at: {}",
      kArchiveMagic, kArchiveVersion, header.value().magic,
      header.value().version, archive_path_.string());
    shader_cache_.clear();
    std::error_code ec;
    std::filesystem::remove(archive_path_, ec);
    return;
  }

  shader_cache_.clear();
  for (size_t i = 0; i < header.value().shader_count; ++i) {
    auto shader_type_value = reader.Read<std::underlying_type_t<ShaderType>>();
    if (!shader_type_value.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + shader_type_value.error().message());
    }

    auto cache_key = reader.Read<uint64_t>();
    if (!cache_key.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + cache_key.error().message());
    }

    auto source_path = reader.Read<std::string>();
    if (!source_path.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + source_path.error().message());
    }

    auto entry_point = reader.Read<std::string>();
    if (!entry_point.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + entry_point.error().message());
    }

    auto defines_count = reader.Read<size_t>();
    if (!defines_count.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + defines_count.error().message());
    }

    std::vector<ShaderDefine> defines;
    defines.reserve(defines_count.value());
    for (size_t d = 0; d < defines_count.value(); ++d) {
      auto name = reader.Read<std::string>();
      if (!name.has_value()) {
        throw std::runtime_error(
          "archive loading error: " + name.error().message());
      }
      auto has_value = reader.Read<uint8_t>();
      if (!has_value.has_value()) {
        throw std::runtime_error(
          "archive loading error: " + has_value.error().message());
      }

      std::optional<std::string> value;
      if (has_value.value() != 0) {
        auto v = reader.Read<std::string>();
        if (!v.has_value()) {
          throw std::runtime_error(
            "archive loading error: " + v.error().message());
        }
        value = v.move_value();
      }

      defines.emplace_back(
        ShaderDefine { .name = name.move_value(), .value = std::move(value) });
    }

    auto source_hash = reader.Read<uint64_t>();
    if (!source_hash.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + source_hash.error().message());
    }

    auto compile_time_ms = reader.Read<int64_t>();
    if (!compile_time_ms.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + compile_time_ms.error().message());
    }

    auto bloc_size = reader.Read<size_t>();
    if (!bloc_size.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + bloc_size.error().message());
    }

    auto binary_data = reader.Read<std::vector<uint32_t>>();
    if (!binary_data.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + binary_data.error().message());
    }

    const auto stage = static_cast<ShaderType>(shader_type_value.value());
    auto request = CanonicalizeShaderRequest(ShaderRequest {
      .stage = stage,
      .source_path = source_path.value(),
      .entry_point = entry_point.value(),
      .defines = std::move(defines),
    });

    CompiledShaderInfo info { std::move(request), cache_key.value(),
      source_hash.value(), bloc_size.value(),
      std::chrono::system_clock::time_point(
        std::chrono::milliseconds(compile_time_ms.value())) };

    auto bytecode = std::make_shared<ShaderByteCode<std::vector<uint32_t>>>(
      binary_data.move_value());
    shader_cache_[info.cache_key]
      = { .info = info, .bytecode = std::move(bytecode) };
  }
}

void ShaderManager::Clear() noexcept
{
  shader_cache_.clear();
  shader_infos_.clear();
}

auto ShaderManager::CompileAndAddShader(const ShaderInfo& profile) -> bool
{
  DCHECK_F(!config_.source_dir.empty(), "No shader source directory specified");

  const auto request = CanonicalizeShaderRequest(ShaderRequest {
    .stage = profile.type,
    .source_path = profile.relative_path,
    .entry_point = profile.entry_point,
    .defines = {},
  });

  const auto cache_key = ComputeShaderRequestKey(request);

  std::filesystem::path source_path
    = config_.source_dir / std::filesystem::path(request.source_path);

  ShaderCompiler::ShaderCompileOptions compile_options {};
  compile_options.include_dirs.emplace_back(config_.source_dir);
  for (const auto& dir : config_.include_dirs) {
    compile_options.include_dirs.emplace_back(dir);
  }
  compile_options.defines = request.defines;

  auto bytecode
    = config_.compiler->CompileFromFile(source_path, profile, compile_options);
  if (!bytecode) {
    return false;
  }

  const auto source_hash
    = ComputeSourceHashWithIncludes(source_path, compile_options.include_dirs);

  CompiledShaderInfo info { request, cache_key, source_hash, bytecode->Size(),
    std::chrono::system_clock::now() };

  return AddCompiledShader(
    { .info = std::move(info), .bytecode = std::move(bytecode) });
}

auto ShaderManager::HasShader(const ShaderRequest& request) const noexcept
  -> bool
{
  const auto canonical = CanonicalizeShaderRequest(ShaderRequest(request));
  return shader_cache_.contains(ComputeShaderRequestKey(canonical));
}

auto ShaderManager::GetShaderCount() const noexcept -> size_t
{
  return shader_cache_.size();
}
