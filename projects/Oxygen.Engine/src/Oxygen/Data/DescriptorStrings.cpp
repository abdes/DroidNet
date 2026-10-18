//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>

#include <Oxygen/Data/DescriptorStrings.h>

namespace oxygen::data {

using pak::core::DescriptorStringTable;
using pak::core::StringTableOffsetT;

DescriptorStrings::DescriptorStrings(std::vector<char> table)
  : table_(std::move(table))
{
  if (!table_.empty() && (table_.front() != '\0' || table_.back() != '\0')) {
    throw std::invalid_argument(
      "descriptor string table must start and end with a NUL byte");
  }
}

auto DescriptorStrings::IsValidLocation(
  const DescriptorStringTable& table, const std::size_t size) noexcept -> bool
{
  return table.size != 0 && table.offset >= sizeof(pak::core::AssetHeader)
    && table.offset <= size && table.size <= size - table.offset;
}

auto DescriptorStrings::FromDescriptor(
  const std::span<const std::byte> descriptor,
  const DescriptorStringTable& table) -> DescriptorStrings
{
  if (!IsValidLocation(table, descriptor.size())) {
    throw std::invalid_argument(
      "descriptor string table lies outside its descriptor");
  }
  auto bytes = std::vector<char>(table.size);
  std::memcpy(
    bytes.data(), descriptor.subspan(table.offset).data(), table.size);
  return DescriptorStrings { std::move(bytes) };
}

auto DescriptorStrings::TryGet(const StringTableOffsetT offset) const noexcept
  -> std::optional<std::string_view>
{
  if (offset == 0) {
    return std::string_view {};
  }
  if (offset >= table_.size()) {
    return std::nullopt;
  }
  // A non-empty table ends with NUL, so the search always finds one.
  const auto tail = std::span { table_ }.subspan(offset);
  const auto end = std::ranges::find(tail, '\0');
  return std::string_view { tail.data(),
    static_cast<std::size_t>(end - tail.begin()) };
}

auto DescriptorStrings::Get(const StringTableOffsetT offset) const
  -> std::string_view
{
  const auto value = TryGet(offset);
  if (!value) {
    throw std::invalid_argument(
      "string offset is outside the descriptor string table");
  }
  return *value;
}

DescriptorStringsBuilder::DescriptorStringsBuilder()
  : bytes_ { '\0' }
{
}

DescriptorStringsBuilder::DescriptorStringsBuilder(
  const DescriptorStrings& existing)
  : bytes_(existing.Bytes().begin(), existing.Bytes().end())
{
  if (bytes_.empty()) {
    bytes_.push_back('\0');
    return;
  }
  // A valid table starts and ends with NUL; index each string at its first
  // offset.
  auto offset = std::size_t { 1 };
  while (offset < bytes_.size()) {
    const auto end = std::find(
      bytes_.begin() + static_cast<std::ptrdiff_t>(offset), bytes_.end(), '\0');
    const auto length = static_cast<std::size_t>(
      end - bytes_.begin() - static_cast<std::ptrdiff_t>(offset));
    offsets_.try_emplace(std::string { bytes_.data() + offset, length },
      static_cast<StringTableOffsetT>(offset));
    offset += length + 1;
  }
}

auto DescriptorStringsBuilder::Add(const std::string_view value)
  -> StringTableOffsetT
{
  if (value.empty()) {
    return 0;
  }
  if (value.find('\0') != std::string_view::npos) {
    throw std::invalid_argument("descriptor strings cannot hold a NUL byte");
  }
  if (const auto it = offsets_.find(std::string { value });
    it != offsets_.end()) {
    return it->second;
  }
  constexpr auto kMaxSize = std::numeric_limits<StringTableOffsetT>::max();
  if (value.size() + 1 > kMaxSize - bytes_.size()) {
    throw std::length_error("descriptor string table exceeds 4 GiB");
  }
  const auto offset = static_cast<StringTableOffsetT>(bytes_.size());
  bytes_.insert(bytes_.end(), value.begin(), value.end());
  bytes_.push_back('\0');
  offsets_.emplace(std::string { value }, offset);
  return offset;
}

auto AppendDescriptorStrings(std::vector<std::byte>& descriptor,
  const DescriptorStringsBuilder& strings) -> void
{
  if (descriptor.size() < sizeof(pak::core::AssetHeader)) {
    throw std::invalid_argument("descriptor is smaller than its header");
  }
  if (descriptor.size() > std::numeric_limits<StringTableOffsetT>::max()) {
    throw std::length_error("descriptor string table offset exceeds 4 GiB");
  }
  const auto table = DescriptorStringTable {
    .offset = static_cast<StringTableOffsetT>(descriptor.size()),
    .size = strings.Size(),
  };
  const auto bytes = strings.Bytes();
  descriptor.insert(descriptor.end(), bytes.begin(), bytes.end());
  std::memcpy(descriptor.data() + offsetof(pak::core::AssetHeader, strings),
    &table, sizeof(table));
}

} // namespace oxygen::data
