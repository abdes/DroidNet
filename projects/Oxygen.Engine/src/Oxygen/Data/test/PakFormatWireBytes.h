//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Writer.h>
#include <Oxygen/Testing/GTest.h>

//! Golden-vector kit for the pak format layout tests: expected wire bytes are
//! written field by field, independently of the structs they check.
namespace oxygen::data::pak::testing {

//! Expected wire bytes, written field by field from the format's documented
//! order and little-endian byte order, independently of the structs.
class WireBytes final {
public:
  //! Appends the low byte of `value`.
  auto U8(const uint64_t value) -> WireBytes& { return Le<1>(value); }

  //! Appends the 2 low bytes of `value`, least significant first.
  auto U16(const uint64_t value) -> WireBytes& { return Le<2>(value); }

  //! Appends the 4 low bytes of `value`, least significant first.
  auto U32(const uint64_t value) -> WireBytes& { return Le<4>(value); }

  //! Appends the 8 bytes of `value`, least significant first.
  auto U64(const uint64_t value) -> WireBytes& { return Le<8>(value); }

  //! Appends the IEEE-754 bits of `value`, least significant byte first.
  auto F32(const float value) -> WireBytes&
  {
    return U32(std::bit_cast<uint32_t>(value));
  }

  //! Appends `count` floats counting up from `first` in steps of one.
  auto Floats(const float first, const size_t count) -> WireBytes&
  {
    for (size_t i = 0; i < count; ++i) {
      F32(first + static_cast<float>(i));
    }
    return *this;
  }

  //! Appends `count` bytes counting up from `first`.
  auto Run(const uint8_t first, const size_t count) -> WireBytes&
  {
    for (size_t i = 0; i < count; ++i) {
      bytes_.push_back(static_cast<std::byte>(first + i));
    }
    return *this;
  }

  //! Appends `count` zero bytes.
  auto Zeros(const size_t count) -> WireBytes&
  {
    bytes_.insert(bytes_.end(), count, std::byte { 0U });
    return *this;
  }

  //! Appends the characters of `text`, without a terminator.
  auto Text(const std::string_view text) -> WireBytes&
  {
    for (const auto c : text) {
      bytes_.push_back(static_cast<std::byte>(c));
    }
    return *this;
  }

  //! Appends the bytes of another expectation.
  auto Append(const WireBytes& other) -> WireBytes&
  {
    bytes_.insert(bytes_.end(), other.bytes_.begin(), other.bytes_.end());
    return *this;
  }

  [[nodiscard]] auto Bytes() const -> const std::vector<std::byte>&
  {
    return bytes_;
  }

private:
  //! Appends the `Size` low bytes of `value`, least significant first.
  template <size_t Size> auto Le(const uint64_t value) -> WireBytes&
  {
    for (size_t i = 0; i < Size; ++i) {
      bytes_.push_back(static_cast<std::byte>((value >> (8U * i)) & 0xFFU));
    }
    return *this;
  }

  std::vector<std::byte> bytes_;
};

//! The bytes Serio writes for `record`.
template <class T> auto Encoded(const T& record) -> std::vector<std::byte>
{
  auto stream = serio::MemoryStream {};
  auto writer = serio::Writer(stream);
  EXPECT_TRUE(writer.Write(record));
  const auto data = stream.Data();
  return { data.begin(), data.end() };
}

//! The in-memory bytes of a packed record that Serio does not write (it is not
//! a Serio record); the struct is packed, so these are its wire bytes.
template <class T> auto RawBytes(const T& record) -> std::vector<std::byte>
{
  const auto bytes = std::bit_cast<std::array<std::byte, sizeof(T)>>(record);
  return { bytes.begin(), bytes.end() };
}

//! A record whose every byte, including the bytes its union arms leave unused,
//! is zero.
template <class T> auto ZeroedRecord() -> T
{
  return std::bit_cast<T>(std::array<std::byte, sizeof(T)> {});
}

//! Sixteen identity bytes counting up from `first`.
inline auto IdBytes(const uint8_t first) -> std::array<uint8_t, 16>
{
  auto bytes = std::array<uint8_t, 16> {};
  for (size_t i = 0; i < bytes.size(); ++i) {
    bytes.at(i) = static_cast<uint8_t>(first + i);
  }
  return bytes;
}

//! An asset key whose bytes count up from `first`.
inline auto Key(const uint8_t first) -> AssetKey
{
  return AssetKey::FromBytes(IdBytes(first));
}

//! A surface id whose bytes count up from `first`.
inline auto Surface(const uint8_t first) -> SurfaceId
{
  return SurfaceId::FromBytes(IdBytes(first));
}

//! A digest whose bytes count up from `first`.
inline auto Digest(const uint8_t first) -> core::ContentHashDigest
{
  auto digest = core::ContentHashDigest {};
  for (size_t i = 0; i < digest.size(); ++i) {
    digest.at(i) = static_cast<uint8_t>(first + i);
  }
  return digest;
}

//! An asset header with distinct values.
inline auto MakeHeader() -> core::AssetHeader
{
  return core::AssetHeader {
    .asset_type = 0x0AU,
    .strings = { .offset = 0x11121314U, .size = 0x21222324U, },
    .name_offset = 0x31323334U,
    .version = 0x41U,
    .streaming_priority = 0x51U,
    .content_hash = Digest(0x60U),
    .variant_flags = 0x71727374U,
  };
}

//! The documented bytes of the header `MakeHeader` returns: type, string
//! table, name, version, priority, content hash and variant flags.
inline auto HeaderBytes() -> WireBytes
{
  auto bytes = WireBytes {};
  bytes.U8(0x0AU)
    .U32(0x11121314U)
    .U32(0x21222324U)
    .U32(0x31323334U)
    .U8(0x41U)
    .U8(0x51U)
    .Run(0x60U, 32)
    .U32(0x71727374U);
  return bytes;
}

} // namespace oxygen::data::pak::testing
