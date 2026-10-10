//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <fstream>
#include <iterator>
#include <stdexcept>

#include <Oxygen/Base/Filesystem.h>
#include <Oxygen/Cooker/Test/Support/FileIo.h>

namespace oxygen::cooker::test {

namespace {

  auto OpenForWrite(const std::filesystem::path& path) -> std::ofstream
  {
    if (const auto parent = path.parent_path(); !parent.empty()) {
      std::filesystem::create_directories(base::ToNativePath(parent));
    }
    auto out = std::ofstream(
      base::ToNativePath(path), std::ios::binary | std::ios::trunc);
    if (!out) {
      throw std::runtime_error("Cannot open for writing: " + path.string());
    }
    return out;
  }

  auto Finish(std::ofstream& out, const std::filesystem::path& path) -> void
  {
    out.close();
    if (!out) {
      throw std::runtime_error("Failed to write: " + path.string());
    }
  }

} // namespace

auto ReadBytes(const std::filesystem::path& path) -> std::vector<std::byte>
{
  auto in = std::ifstream(base::ToNativePath(path), std::ios::binary);
  if (!in) {
    throw std::runtime_error("Cannot open for reading: " + path.string());
  }
  const auto chars = std::vector<char>(
    std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  auto bytes = std::vector<std::byte>(chars.size());
  for (size_t i = 0; i < chars.size(); ++i) {
    bytes[i] = static_cast<std::byte>(chars[i]);
  }
  return bytes;
}

auto ReadText(const std::filesystem::path& path) -> std::string
{
  auto in = std::ifstream(base::ToNativePath(path), std::ios::binary);
  if (!in) {
    throw std::runtime_error("Cannot open for reading: " + path.string());
  }
  return { std::istreambuf_iterator<char>(in),
    std::istreambuf_iterator<char>() };
}

auto WriteBytes(const std::filesystem::path& path,
  const std::span<const std::byte> bytes) -> void
{
  auto out = OpenForWrite(path);
  if (!bytes.empty()) {
    // NOLINTNEXTLINE(*-reinterpret-cast)
    out.write(reinterpret_cast<const char*>(bytes.data()),
      static_cast<std::streamsize>(bytes.size()));
  }
  Finish(out, path);
}

auto WriteText(const std::filesystem::path& path, const std::string_view text)
  -> void
{
  auto out = OpenForWrite(path);
  out.write(text.data(), static_cast<std::streamsize>(text.size()));
  Finish(out, path);
}

} // namespace oxygen::cooker::test
