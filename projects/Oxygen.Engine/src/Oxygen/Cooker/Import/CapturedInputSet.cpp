//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include <nlohmann/json-schema.hpp>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Cooker/Import/CapturedInputSet.h>
#include <Oxygen/Cooker/Import/FileInfo.h>
#include <Oxygen/Cooker/Import/Internal/ImportManifest_schema.h>

namespace oxygen::content::import {
namespace {
  auto ReadPath(const nlohmann::json& value) -> std::filesystem::path
  {
    const auto text = value.get<std::string>();
    if (text.find('\0') != std::string::npos) {
      throw std::invalid_argument(
        "Captured paths cannot contain null characters");
    }
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
  }

  auto ReadSize(const nlohmann::json& value) -> uint64_t
  {
    if (value.is_number_unsigned()) {
      return value.get<uint64_t>();
    }
    if (value.is_number_integer()) {
      const auto size = value.get<int64_t>();
      if (size >= 0) {
        return static_cast<uint64_t>(size);
      }
    }
    throw std::invalid_argument(
      "Captured sizes must be unsigned 64-bit integers");
  }

  auto ReadDigest(const std::string& text) -> base::Sha256Digest
  {
    auto digest = base::Sha256Digest {};
    constexpr auto kHexCharactersPerByte = 2U;
    constexpr auto kHexRadix = 16;
    for (size_t index = 0; index < digest.size(); ++index) {
      const auto* begin = text.data() + index * kHexCharactersPerByte;
      const auto* end = begin + kHexCharactersPerByte;
      const auto parsed
        = std::from_chars(begin, end, digest.at(index), kHexRadix);
      if (parsed.ec != std::errc {} || parsed.ptr != end) {
        throw std::invalid_argument("Invalid captured source digest");
      }
    }
    return digest;
  }

  auto ReadTimestamp(const nlohmann::json& value)
    -> std::filesystem::file_time_type
  {
    using namespace std::chrono;
    using FileDuration = std::filesystem::file_time_type::duration;
    const auto whole_seconds = clock_cast<file_clock>(
      sys_seconds { seconds { value.at("last_modified_seconds")
          .get<int64_t>() } }).time_since_epoch();
    const auto nanos
      = nanoseconds { value.at("last_modified_nanoseconds").get<int64_t>() };
    const auto fraction = duration_cast<FileDuration>(nanos);
    if (duration_cast<nanoseconds>(fraction) != nanos
      || whole_seconds < ceil<seconds>(FileDuration::min())
      || whole_seconds > floor<seconds>(FileDuration::max() - fraction)) {
      throw std::invalid_argument(
        "Captured timestamp is not representable by the native file clock");
    }
    return std::filesystem::file_time_type {
      duration_cast<FileDuration>(whole_seconds) + fraction
    };
  }
} // namespace

CapturedInputSet::CapturedInputSet(const std::span<const CapturedInput> inputs)
{
  inputs_.reserve(inputs.size());
  indices_.reserve(inputs.size());
  for (const auto& input : inputs) {
    if (input.logical_path.empty() || !input.logical_path.is_absolute()) {
      throw std::invalid_argument("Captured logical paths must be absolute");
    }
    if (!input.exists
      && (input.metadata.has_value() || input.file.has_value())) {
      throw std::invalid_argument(
        "Absent captured sources cannot contain metadata or bytes");
    }
    if (input.file.has_value()) {
      if (input.file->path.empty() || !input.file->path.is_absolute()) {
        throw std::invalid_argument("Captured byte paths must be absolute");
      }
      if (!input.metadata.has_value() || input.metadata->is_directory
        || input.metadata->size != input.file->size) {
        throw std::invalid_argument(
          "Captured bytes require matching source-file metadata");
      }
    }
    if (!indices_
          .emplace(base::PathIdentityKey(input.logical_path), inputs_.size())
          .second) {
      throw std::invalid_argument(
        "Duplicate captured source identity: " + input.logical_path.string());
    }
    inputs_.push_back(input);
  }
}

auto CapturedInputSet::Find(const std::filesystem::path& logical_path) const
  -> const CapturedInput*
{
  const auto entry = indices_.find(base::PathIdentityKey(logical_path));
  return entry == indices_.end() ? nullptr : &inputs_.at(entry->second);
}

auto CapturedInputSet::Parse(const std::string_view text)
  -> std::shared_ptr<const CapturedInputSet>
{
  const auto document = nlohmann::json::parse(text);
  auto validator = nlohmann::json_schema::json_validator {};
  validator.set_root_schema(nlohmann::json::parse(kCapturedInputsSchema));
  validator.validate(document);
  auto inputs = std::vector<CapturedInput> {};
  inputs.reserve(document.at("inputs").size());
  for (const auto& entry : document.at("inputs")) {
    auto input = CapturedInput {
      .logical_path = ReadPath(entry.at("logical_path")),
      .exists = entry.at("exists").get<bool>(),
      .metadata = {},
      .file = {},
    };
    const auto& metadata = entry.at("metadata");
    if (!metadata.is_null()) {
      input.metadata = FileInfo {
        .size = ReadSize(metadata.at("size")),
        .last_modified = ReadTimestamp(metadata),
        .is_directory = metadata.at("is_directory").get<bool>(),
        .is_symlink = metadata.at("is_symlink").get<bool>(),
      };
    }
    const auto& file = entry.at("file");
    if (!file.is_null()) {
      input.file = CapturedInputFile {
        .path = ReadPath(file.at("path")),
        .size = ReadSize(file.at("size")),
        .digest = ReadDigest(file.at("sha256").get<std::string>()),
      };
    }
    inputs.push_back(std::move(input));
  }
  return std::make_shared<const CapturedInputSet>(inputs);
}

} // namespace oxygen::content::import
