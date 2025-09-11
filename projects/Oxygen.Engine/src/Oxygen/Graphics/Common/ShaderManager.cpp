//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Graphics/Common/ShaderManager.h>

#include <chrono>
#include <filesystem>
#include <ranges>
#include <span>
#include <string>

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

auto CalculateShaderSourceHash(const std::u8string& shader_source) -> uint64_t
{
  return oxygen::ComputeFNV1a64(shader_source.data(), shader_source.size());
}

auto ComputeSourceHash(const std::filesystem::path& source_path) -> uint64_t
{
  std::ifstream file(source_path, std::ios::binary);
  if (!file) {
    return 0;
  }

  std::string content(
    (std::istreambuf_iterator(file)), std::istreambuf_iterator<char>());
  return CalculateShaderSourceHash(
    std::u8string(content.begin(), content.end()));
}

auto IsSourceFileNewer(const CompiledShaderInfo& info) -> bool
{
  const std::filesystem::path source_path(info.source_file_path);
  if (!exists(source_path)) {
    return true;
  }

  const auto file_time = last_write_time(source_path);
  const auto compile_time = info.compile_time;

  // Convert file_time to system_clock::time_point
  const auto file_time_tp
    = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
      file_time - std::filesystem::file_time_type::clock::now()
      + std::chrono::system_clock::now());

  return file_time_tp > compile_time;
}

} // namespace

namespace {
constexpr uint32_t kArchiveMagic = 0x4F585348; // "OXSH"
constexpr uint32_t kArchiveVersion = 1;
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
  std::underlying_type_t<ShaderType> shader_type_value;
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

// Helper: Get the workspace root depending on environment (local or CI)
auto GetWorkspaceRoot() -> std::filesystem::path
{
  // Detect GitHub Actions CI for running tests during CI
  const char* github_actions = std::getenv("GITHUB_ACTIONS");
  if (github_actions && std::string(github_actions) == "true") {
    // Use GITHUB_WORKSPACE as base if set
    const char* github_workspace = std::getenv("GITHUB_WORKSPACE");
    if (github_workspace) {
      return github_workspace;
    } else {
      auto cwd = std::filesystem::current_path();
      LOG_F(WARNING, "GITHUB_WORKSPACE not set, using current directory: {}",
        cwd.string());
      return cwd;
    }
  } else {
    // FIXME: replace hardcoded path
    // Local/dev: use hardcoded project root
    return "F:/projects/DroidNet/projects/Oxygen.Engine/";
  }
}

auto GetArchivePath(const ShaderManager::Config& config)
  -> std::filesystem::path
{
  std::filesystem::path archive_path = GetWorkspaceRoot();

  if (config.archive_dir) {
    archive_path /= *config.archive_dir;
    // Ensure the archive directory exists
    try {
      create_directories(archive_path);
    } catch (const std::filesystem::filesystem_error& e) {
      LOG_F(ERROR, "Failed to create archive directory `{}`: {}",
        archive_path.string(), e.what());
      throw;
    }
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
  DCHECK_F(
    config_.source_dir.has_value(), "No shader source directory specified");

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

  shader_cache_[shader.info.shader_unique_id] = std::move(shader);
  return true;
}

auto ShaderManager::GetShaderBytecode(std::string_view unique_id) const
  -> std::shared_ptr<IShaderByteCode>
{
  const auto it = std::ranges::find_if(shader_cache_, [&](const auto& pair) {
    return pair.second.info.shader_unique_id == unique_id;
  });

  if (it == shader_cache_.end()) {
    return nullptr;
  }

  return it->second.bytecode;
}

auto ShaderManager::IsShaderOutdated(const ShaderInfo& shader) const -> bool
{
  const auto& shader_id = MakeShaderIdentifier(shader);
  const auto it = shader_cache_.find(shader_id);
  if (it == shader_cache_.end()) {
    return true;
  }

  // Check file exists and hash matches
  const auto& info = it->second.info;
  if (const auto current_hash = ComputeSourceHash(info.source_file_path);
    current_hash != info.source_hash) {
    return true;
  }

  return IsSourceFileNewer(info);
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

  const auto result = std::ranges::all_of(outdated,
    [this](const auto& profile) { return CompileAndAddShader(profile); });

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
  return std::ranges::all_of(shader_infos_,
    [this](const auto& profile) { return CompileAndAddShader(profile); });
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
    if (auto result = writer.Write(info.shader_type); !result.has_value()) {
      throw std::runtime_error("archive saving error: shader type");
    }
    if (auto result = writer.Write<std::string>(info.shader_unique_id);
      !result.has_value()) {
      throw std::runtime_error("archive saving error: shader unique id");
    }
    if (auto result = writer.Write<std::string>(info.source_file_path);
      !result.has_value()) {
      throw std::runtime_error("archive saving error: source file path");
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
    throw std::runtime_error(
      "archive loading error: " + header.error().message());
  }

  if (header.value().magic != kArchiveMagic
    || header.value().version != kArchiveVersion) {
    throw std::runtime_error("archive loading error: invalid header");
  }

  shader_cache_.clear();
  for (size_t i = 0; i < header.value().shader_count; ++i) {
    auto shader_type = reader.Read<ShaderType>();
    if (!shader_type.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + shader_type.error().message());
    }

    auto unique_id = reader.Read<std::string>();
    if (!unique_id.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + unique_id.error().message());
    }

    auto source_path = reader.Read<std::string>();
    if (!source_path.has_value()) {
      throw std::runtime_error(
        "archive loading error: " + source_path.error().message());
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

    CompiledShaderInfo info { shader_type.value(), unique_id.value(),
      source_path.value(), source_hash.value(), bloc_size.value(),
      std::chrono::system_clock::time_point(
        std::chrono::milliseconds(compile_time_ms.value())) };

    auto bytecode = std::make_shared<ShaderByteCode<std::vector<uint32_t>>>(
      binary_data.move_value());
    shader_cache_[info.shader_unique_id]
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
  DCHECK_F(
    config_.source_dir.has_value(), "No shader source directory specified");

  // Use workspace root as base for source_path
  std::filesystem::path source_path = GetWorkspaceRoot();
  source_path /= config_.source_dir.value();
  source_path /= profile.relative_path;

  auto bytecode = config_.compiler->CompileFromFile(source_path, profile);
  if (!bytecode) {
    return false;
  }

  const auto source_hash = ComputeSourceHash(source_path);

  CompiledShaderInfo info { profile.type, MakeShaderIdentifier(profile),
    source_path.string(), source_hash, bytecode->Size(),
    std::chrono::system_clock::now() };

  return AddCompiledShader(
    { .info = std::move(info), .bytecode = std::move(bytecode) });
}

auto ShaderManager::HasShader(std::string_view unique_id) const noexcept -> bool
{
  return std::ranges::any_of(shader_cache_, [&](const auto& pair) {
    return pair.second.info.shader_unique_id == unique_id;
  });
}

auto ShaderManager::GetShaderCount() const noexcept -> size_t
{
  return shader_cache_.size();
}
