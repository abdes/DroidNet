//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Content/Internal/Sha256.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <stdexcept>

namespace oxygen::content::internal {

namespace {

  constexpr std::array<uint32_t, 64> kK = {
    0x428a2f98u,
    0x71374491u,
    0xb5c0fbcfu,
    0xe9b5dba5u,
    0x3956c25bu,
    0x59f111f1u,
    0x923f82a4u,
    0xab1c5ed5u,
    0xd807aa98u,
    0x12835b01u,
    0x243185beu,
    0x550c7dc3u,
    0x72be5d74u,
    0x80deb1feu,
    0x9bdc06a7u,
    0xc19bf174u,
    0xe49b69c1u,
    0xefbe4786u,
    0x0fc19dc6u,
    0x240ca1ccu,
    0x2de92c6fu,
    0x4a7484aau,
    0x5cb0a9dcu,
    0x76f988dau,
    0x983e5152u,
    0xa831c66du,
    0xb00327c8u,
    0xbf597fc7u,
    0xc6e00bf3u,
    0xd5a79147u,
    0x06ca6351u,
    0x14292967u,
    0x27b70a85u,
    0x2e1b2138u,
    0x4d2c6dfcu,
    0x53380d13u,
    0x650a7354u,
    0x766a0abbu,
    0x81c2c92eu,
    0x92722c85u,
    0xa2bfe8a1u,
    0xa81a664bu,
    0xc24b8b70u,
    0xc76c51a3u,
    0xd192e819u,
    0xd6990624u,
    0xf40e3585u,
    0x106aa070u,
    0x19a4c116u,
    0x1e376c08u,
    0x2748774cu,
    0x34b0bcb5u,
    0x391c0cb3u,
    0x4ed8aa4au,
    0x5b9cca4fu,
    0x682e6ff3u,
    0x748f82eeu,
    0x78a5636fu,
    0x84c87814u,
    0x8cc70208u,
    0x90befffau,
    0xa4506cebu,
    0xbef9a3f7u,
    0xc67178f2u,
  };

  constexpr auto RotR(const uint32_t x, const uint32_t n) noexcept -> uint32_t
  {
    return (x >> n) | (x << (32u - n));
  }

  constexpr auto Ch(
    const uint32_t x, const uint32_t y, const uint32_t z) noexcept -> uint32_t
  {
    return (x & y) ^ ((~x) & z);
  }

  constexpr auto Maj(
    const uint32_t x, const uint32_t y, const uint32_t z) noexcept -> uint32_t
  {
    return (x & y) ^ (x & z) ^ (y & z);
  }

  constexpr auto BigSigma0(const uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22);
  }

  constexpr auto BigSigma1(const uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25);
  }

  constexpr auto SmallSigma0(const uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3);
  }

  constexpr auto SmallSigma1(const uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10);
  }

  constexpr auto ReadBE32(std::span<const std::byte, 4> bytes) noexcept
    -> uint32_t
  {
    const auto b0 = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[0]));
    const auto b1 = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[1]));
    const auto b2 = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[2]));
    const auto b3 = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[3]));
    return (b0 << 24u) | (b1 << 16u) | (b2 << 8u) | b3;
  }

  auto WriteBE32(uint32_t value, std::span<uint8_t, 4> out) noexcept -> void
  {
    out[0] = static_cast<uint8_t>((value >> 24u) & 0xffu);
    out[1] = static_cast<uint8_t>((value >> 16u) & 0xffu);
    out[2] = static_cast<uint8_t>((value >> 8u) & 0xffu);
    out[3] = static_cast<uint8_t>(value & 0xffu);
  }

  auto WriteBE64(uint64_t value, std::span<uint8_t, 8> out) noexcept -> void
  {
    for (size_t i = 0; i < 8; ++i) {
      const auto shift = static_cast<uint32_t>(56u - 8u * i);
      out[i] = static_cast<uint8_t>((value >> shift) & 0xffu);
    }
  }

} // namespace

Sha256::Sha256() noexcept
{
  state_ = {
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u,
  };
}

auto Sha256::Update(std::span<const std::byte> data) noexcept -> void
{
  total_bits_ += static_cast<uint64_t>(data.size()) * 8u;

  size_t offset = 0;

  if (buffer_size_ != 0) {
    const auto need = 64u - buffer_size_;
    const auto take = std::min<size_t>(need, data.size());
    std::copy_n(data.begin(), take, buffer_.begin() + buffer_size_);
    buffer_size_ += take;
    offset += take;

    if (buffer_size_ == 64u) {
      ProcessBlock_(
        std::span<const std::byte, 64>(buffer_.data(), buffer_.size()));
      buffer_size_ = 0;
    }
  }

  while (offset + 64u <= data.size()) {
    ProcessBlock_(std::span<const std::byte, 64>(data.data() + offset, 64u));
    offset += 64u;
  }

  const auto remaining = data.size() - offset;
  if (remaining != 0) {
    std::copy_n(data.begin() + static_cast<std::ptrdiff_t>(offset), remaining,
      buffer_.begin());
    buffer_size_ = remaining;
  }
}

auto Sha256::Finalize() noexcept -> Sha256Digest
{
  std::array<std::byte, 64> block = {};

  std::copy_n(buffer_.begin(), buffer_size_, block.begin());
  block[buffer_size_] = std::byte { 0x80 };

  if (buffer_size_ >= 56u) {
    ProcessBlock_(std::span<const std::byte, 64>(block.data(), block.size()));
    block.fill(std::byte { 0 });
  }

  std::array<uint8_t, 8> len_bytes = {};
  WriteBE64(total_bits_,
    std::span<uint8_t, 8>(
      len_bytes.data(), static_cast<size_t>(len_bytes.size())));

  std::copy_n(reinterpret_cast<const std::byte*>(len_bytes.data()),
    static_cast<size_t>(len_bytes.size()), block.begin() + 56);

  ProcessBlock_(std::span<const std::byte, 64>(block.data(), block.size()));

  Sha256Digest out = {};
  for (size_t i = 0; i < state_.size(); ++i) {
    std::array<uint8_t, 4> tmp = {};
    WriteBE32(state_[i], std::span<uint8_t, 4>(tmp.data(), tmp.size()));
    std::copy_n(tmp.begin(), tmp.size(), out.begin() + i * 4);
  }

  return out;
}

auto Sha256::ProcessBlock_(std::span<const std::byte, 64> block) noexcept
  -> void
{
  std::array<uint32_t, 64> w = {};

  for (size_t i = 0; i < 16; ++i) {
    const auto base = i * 4;
    w[i] = ReadBE32(std::span<const std::byte, 4>(block.data() + base, 4));
  }

  for (size_t i = 16; i < 64; ++i) {
    w[i]
      = SmallSigma1(w[i - 2]) + w[i - 7] + SmallSigma0(w[i - 15]) + w[i - 16];
  }

  uint32_t a = state_[0];
  uint32_t b = state_[1];
  uint32_t c = state_[2];
  uint32_t d = state_[3];
  uint32_t e = state_[4];
  uint32_t f = state_[5];
  uint32_t g = state_[6];
  uint32_t h = state_[7];

  for (size_t i = 0; i < 64; ++i) {
    const auto t1 = h + BigSigma1(e) + Ch(e, f, g) + kK[i] + w[i];
    const auto t2 = BigSigma0(a) + Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

  state_[0] += a;
  state_[1] += b;
  state_[2] += c;
  state_[3] += d;
  state_[4] += e;
  state_[5] += f;
  state_[6] += g;
  state_[7] += h;
}

auto ComputeSha256(std::span<const std::byte> data) noexcept -> Sha256Digest
{
  Sha256 hasher;
  hasher.Update(data);
  return hasher.Finalize();
}

auto IsAllZero(const Sha256Digest& digest) noexcept -> bool
{
  return std::ranges::all_of(digest, [](const uint8_t b) { return b == 0; });
}

auto ComputeFileSha256(const std::filesystem::path& path) -> Sha256Digest
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error(
      "Failed to open file for SHA-256: " + path.string());
  }

  Sha256 hasher;

  std::array<std::byte, 64 * 1024> buffer = {};
  while (true) {
    in.read(reinterpret_cast<char*>(buffer.data()),
      static_cast<std::streamsize>(buffer.size()));
    const auto got = in.gcount();
    if (got > 0) {
      hasher.Update(
        std::span<const std::byte>(buffer.data(), static_cast<size_t>(got)));
    }

    if (!in) {
      if (in.eof()) {
        break;
      }
      throw std::runtime_error(
        "Failed while reading file for SHA-256: " + path.string());
    }
  }

  return hasher.Finalize();
}

} // namespace oxygen::content::internal
