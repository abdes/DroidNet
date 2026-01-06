//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Base/Sha256.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#  include <immintrin.h>
#  include <intrin.h>
#  define OXYGEN_SHA_HAS_SHANI 1
#else
#  define OXYGEN_SHA_HAS_SHANI 0
#endif

namespace oxygen::base {

namespace {

  // SHA-256 round constants
  alignas(64) constexpr std::array<uint32_t, 64> kK = {
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

  // Initial hash values
  constexpr std::array<uint32_t, 8> kInitState = {
    0x6a09e667u,
    0xbb67ae85u,
    0x3c6ef372u,
    0xa54ff53au,
    0x510e527fu,
    0x9b05688cu,
    0x1f83d9abu,
    0x5be0cd19u,
  };

  // Rotate right
  [[nodiscard]] constexpr auto RotR(uint32_t x, uint32_t n) noexcept -> uint32_t
  {
    return (x >> n) | (x << (32u - n));
  }

  // SHA-256 functions
  [[nodiscard]] constexpr auto Ch(uint32_t x, uint32_t y, uint32_t z) noexcept
    -> uint32_t
  {
    return (x & y) ^ ((~x) & z);
  }

  [[nodiscard]] constexpr auto Maj(uint32_t x, uint32_t y, uint32_t z) noexcept
    -> uint32_t
  {
    return (x & y) ^ (x & z) ^ (y & z);
  }

  [[nodiscard]] constexpr auto Sigma0(uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 2) ^ RotR(x, 13) ^ RotR(x, 22);
  }

  [[nodiscard]] constexpr auto Sigma1(uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 6) ^ RotR(x, 11) ^ RotR(x, 25);
  }

  [[nodiscard]] constexpr auto sigma0(uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 7) ^ RotR(x, 18) ^ (x >> 3);
  }

  [[nodiscard]] constexpr auto sigma1(uint32_t x) noexcept -> uint32_t
  {
    return RotR(x, 17) ^ RotR(x, 19) ^ (x >> 10);
  }

  // Read 32-bit big-endian value
  [[nodiscard]] inline auto LoadBE32(const std::byte* ptr) noexcept -> uint32_t
  {
    const auto* p = reinterpret_cast<const uint8_t*>(ptr);
    return (static_cast<uint32_t>(p[0]) << 24u)
      | (static_cast<uint32_t>(p[1]) << 16u)
      | (static_cast<uint32_t>(p[2]) << 8u) | static_cast<uint32_t>(p[3]);
  }

  // Write 32-bit big-endian value
  inline auto StoreBE32(uint8_t* ptr, uint32_t val) noexcept -> void
  {
    ptr[0] = static_cast<uint8_t>(val >> 24u);
    ptr[1] = static_cast<uint8_t>(val >> 16u);
    ptr[2] = static_cast<uint8_t>(val >> 8u);
    ptr[3] = static_cast<uint8_t>(val);
  }

  // Write 64-bit big-endian value
  inline auto StoreBE64(uint8_t* ptr, uint64_t val) noexcept -> void
  {
    ptr[0] = static_cast<uint8_t>(val >> 56u);
    ptr[1] = static_cast<uint8_t>(val >> 48u);
    ptr[2] = static_cast<uint8_t>(val >> 40u);
    ptr[3] = static_cast<uint8_t>(val >> 32u);
    ptr[4] = static_cast<uint8_t>(val >> 24u);
    ptr[5] = static_cast<uint8_t>(val >> 16u);
    ptr[6] = static_cast<uint8_t>(val >> 8u);
    ptr[7] = static_cast<uint8_t>(val);
  }

#if OXYGEN_SHA_HAS_SHANI
  // Cached CPU feature detection
  [[nodiscard]] auto DetectShaNi() noexcept -> bool
  {
    int cpu_info[4] = {};
    __cpuid(cpu_info, 0);
    const int max_leaf = cpu_info[0];
    if (max_leaf < 7) {
      return false;
    }
    __cpuidex(cpu_info, 7, 0);
    // SHA-NI is bit 29 of EBX
    return (cpu_info[1] & (1 << 29)) != 0;
  }

  [[nodiscard]] auto HasShaNi() noexcept -> bool
  {
    static const bool kHasShaNi = DetectShaNi();
    return kHasShaNi;
  }
#endif

  // One SHA-256 round (software)
  inline auto Round(uint32_t& a, uint32_t& b, uint32_t& c, uint32_t& d,
    uint32_t& e, uint32_t& f, uint32_t& g, uint32_t& h, uint32_t k,
    uint32_t w) noexcept -> void
  {
    const uint32_t t1 = h + Sigma1(e) + Ch(e, f, g) + k + w;
    const uint32_t t2 = Sigma0(a) + Maj(a, b, c);
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }

} // namespace

Sha256::Sha256() noexcept
  : state_(kInitState)
{
}

auto Sha256::HasHardwareSupport() noexcept -> bool
{
#if OXYGEN_SHA_HAS_SHANI
  return HasShaNi();
#else
  return false;
#endif
}

auto Sha256::Update(std::span<const std::byte> data) noexcept -> void
{
  if (data.empty()) {
    return;
  }

  const auto* ptr = data.data();
  size_t remaining = data.size();

  total_bytes_ += remaining;

  // If we have buffered data, try to complete a block
  if (buffer_size_ != 0) {
    const size_t need = kBlockSize - buffer_size_;
    const size_t take = std::min(need, remaining);
    std::memcpy(buffer_.data() + buffer_size_, ptr, take);
    buffer_size_ += take;
    ptr += take;
    remaining -= take;

    if (buffer_size_ == kBlockSize) {
      ProcessBlocks_(buffer_.data(), 1);
      buffer_size_ = 0;
    }
  }

  // Process complete blocks directly from input
  if (remaining >= kBlockSize) {
    const size_t blocks = remaining / kBlockSize;
    ProcessBlocks_(ptr, blocks);
    const size_t processed = blocks * kBlockSize;
    ptr += processed;
    remaining -= processed;
  }

  // Buffer remaining bytes
  if (remaining != 0) {
    std::memcpy(buffer_.data(), ptr, remaining);
    buffer_size_ = remaining;
  }
}

auto Sha256::Finalize() noexcept -> Sha256Digest
{
  // Pad message
  const uint64_t total_bits = total_bytes_ * 8;

  // Append 0x80 byte
  buffer_[buffer_size_++] = std::byte { 0x80 };

  // If not enough room for length, pad and process
  if (buffer_size_ > 56) {
    std::memset(buffer_.data() + buffer_size_, 0, kBlockSize - buffer_size_);
    ProcessBlocks_(buffer_.data(), 1);
    buffer_size_ = 0;
  }

  // Pad to 56 bytes and append length
  std::memset(buffer_.data() + buffer_size_, 0, 56 - buffer_size_);
  StoreBE64(reinterpret_cast<uint8_t*>(buffer_.data() + 56), total_bits);
  ProcessBlocks_(buffer_.data(), 1);

  // Output digest
  Sha256Digest digest = {};
  for (size_t i = 0; i < 8; ++i) {
    StoreBE32(digest.data() + i * 4, state_[i]);
  }

  // Reset state for potential reuse
  state_ = kInitState;
  total_bytes_ = 0;
  buffer_size_ = 0;

  return digest;
}

auto Sha256::ProcessBlocks_(const std::byte* data, size_t block_count) noexcept
  -> void
{
#if OXYGEN_SHA_HAS_SHANI
  if (HasShaNi()) {
    ProcessBlocksShaNi_(data, block_count);
    return;
  }
#endif
  ProcessBlocksSoftware_(data, block_count);
}

// Optimized software implementation with partial loop unrolling
auto Sha256::ProcessBlocksSoftware_(
  const std::byte* data, size_t block_count) noexcept -> void
{
  alignas(64) std::array<uint32_t, 64> w;

  for (size_t blk = 0; blk < block_count; ++blk) {
    const std::byte* block = data + blk * kBlockSize;

    // Load and expand message schedule
    for (size_t i = 0; i < 16; ++i) {
      w[i] = LoadBE32(block + i * 4);
    }
    for (size_t i = 16; i < 64; ++i) {
      w[i] = sigma1(w[i - 2]) + w[i - 7] + sigma0(w[i - 15]) + w[i - 16];
    }

    // Initialize working variables
    uint32_t a = state_[0];
    uint32_t b = state_[1];
    uint32_t c = state_[2];
    uint32_t d = state_[3];
    uint32_t e = state_[4];
    uint32_t f = state_[5];
    uint32_t g = state_[6];
    uint32_t h = state_[7];

    // 64 rounds, unrolled 8 at a time
    for (size_t i = 0; i < 64; i += 8) {
      Round(a, b, c, d, e, f, g, h, kK[i + 0], w[i + 0]);
      Round(a, b, c, d, e, f, g, h, kK[i + 1], w[i + 1]);
      Round(a, b, c, d, e, f, g, h, kK[i + 2], w[i + 2]);
      Round(a, b, c, d, e, f, g, h, kK[i + 3], w[i + 3]);
      Round(a, b, c, d, e, f, g, h, kK[i + 4], w[i + 4]);
      Round(a, b, c, d, e, f, g, h, kK[i + 5], w[i + 5]);
      Round(a, b, c, d, e, f, g, h, kK[i + 6], w[i + 6]);
      Round(a, b, c, d, e, f, g, h, kK[i + 7], w[i + 7]);
    }

    // Add compressed chunk to current hash value
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }
}

#if OXYGEN_SHA_HAS_SHANI
// Intel SHA-NI hardware-accelerated implementation
auto Sha256::ProcessBlocksShaNi_(
  const std::byte* data, size_t block_count) noexcept -> void
{
  // SHA-NI requires these constants for byte shuffling to big-endian
  const __m128i kShufMask
    = _mm_set_epi64x(0x0c0d0e0f08090a0bULL, 0x0405060700010203ULL);

  // Load initial state: state_[0..7] = A B C D E F G H
  // In memory as 128-bit loads:
  //   tmp0 = lanes[3][2][1][0] = D C B A
  //   tmp1 = lanes[3][2][1][0] = H G F E
  __m128i tmp0 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[0]));
  __m128i tmp1 = _mm_loadu_si128(reinterpret_cast<const __m128i*>(&state_[4]));

  // SHA-NI expects: STATE0 = [A][B][E][F], STATE1 = [C][D][G][H] in lanes
  // [3][2][1][0] Swap pairs: DCBA -> CDAB, HGFE -> GHEF
  tmp0 = _mm_shuffle_epi32(tmp0, 0xB1); // C D A B
  tmp1 = _mm_shuffle_epi32(tmp1, 0xB1); // G H E F

  // Interleave: STATE0 gets [A][B] from tmp0 lower and [E][F] from tmp1 lower
  //             STATE1 gets [C][D] from tmp0 upper and [G][H] from tmp1 upper
  __m128i state0 = _mm_unpacklo_epi64(tmp1, tmp0); // [A][B][E][F]
  __m128i state1 = _mm_unpackhi_epi64(tmp1, tmp0); // [C][D][G][H]
  __m128i tmp;

  for (size_t blk = 0; blk < block_count; ++blk) {
    const __m128i* msg_ptr
      = reinterpret_cast<const __m128i*>(data + blk * kBlockSize);

    // Save current state
    const __m128i save_state0 = state0;
    const __m128i save_state1 = state1;

    // Load message and convert to big-endian
    __m128i msg0 = _mm_shuffle_epi8(_mm_loadu_si128(msg_ptr + 0), kShufMask);
    __m128i msg1 = _mm_shuffle_epi8(_mm_loadu_si128(msg_ptr + 1), kShufMask);
    __m128i msg2 = _mm_shuffle_epi8(_mm_loadu_si128(msg_ptr + 2), kShufMask);
    __m128i msg3 = _mm_shuffle_epi8(_mm_loadu_si128(msg_ptr + 3), kShufMask);

    // Rounds 0-3
    __m128i msg = _mm_add_epi32(
      msg0, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[0])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

    // Rounds 4-7
    msg = _mm_add_epi32(
      msg1, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[4])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Rounds 8-11
    msg = _mm_add_epi32(
      msg2, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[8])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Rounds 12-15
    msg = _mm_add_epi32(
      msg3, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[12])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg3, msg2, 4);
    msg0 = _mm_add_epi32(msg0, tmp);
    msg0 = _mm_sha256msg2_epu32(msg0, msg3);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Rounds 16-19
    msg = _mm_add_epi32(
      msg0, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[16])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg0, msg3, 4);
    msg1 = _mm_add_epi32(msg1, tmp);
    msg1 = _mm_sha256msg2_epu32(msg1, msg0);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Rounds 20-23
    msg = _mm_add_epi32(
      msg1, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[20])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg1, msg0, 4);
    msg2 = _mm_add_epi32(msg2, tmp);
    msg2 = _mm_sha256msg2_epu32(msg2, msg1);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Rounds 24-27
    msg = _mm_add_epi32(
      msg2, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[24])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg2, msg1, 4);
    msg3 = _mm_add_epi32(msg3, tmp);
    msg3 = _mm_sha256msg2_epu32(msg3, msg2);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Rounds 28-31
    msg = _mm_add_epi32(
      msg3, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[28])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg3, msg2, 4);
    msg0 = _mm_add_epi32(msg0, tmp);
    msg0 = _mm_sha256msg2_epu32(msg0, msg3);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Rounds 32-35
    msg = _mm_add_epi32(
      msg0, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[32])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg0, msg3, 4);
    msg1 = _mm_add_epi32(msg1, tmp);
    msg1 = _mm_sha256msg2_epu32(msg1, msg0);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Rounds 36-39
    msg = _mm_add_epi32(
      msg1, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[36])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg1, msg0, 4);
    msg2 = _mm_add_epi32(msg2, tmp);
    msg2 = _mm_sha256msg2_epu32(msg2, msg1);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg0 = _mm_sha256msg1_epu32(msg0, msg1);

    // Rounds 40-43
    msg = _mm_add_epi32(
      msg2, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[40])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg2, msg1, 4);
    msg3 = _mm_add_epi32(msg3, tmp);
    msg3 = _mm_sha256msg2_epu32(msg3, msg2);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg1 = _mm_sha256msg1_epu32(msg1, msg2);

    // Rounds 44-47
    msg = _mm_add_epi32(
      msg3, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[44])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg3, msg2, 4);
    msg0 = _mm_add_epi32(msg0, tmp);
    msg0 = _mm_sha256msg2_epu32(msg0, msg3);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg2 = _mm_sha256msg1_epu32(msg2, msg3);

    // Rounds 48-51
    msg = _mm_add_epi32(
      msg0, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[48])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg0, msg3, 4);
    msg1 = _mm_add_epi32(msg1, tmp);
    msg1 = _mm_sha256msg2_epu32(msg1, msg0);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);
    msg3 = _mm_sha256msg1_epu32(msg3, msg0);

    // Rounds 52-55
    msg = _mm_add_epi32(
      msg1, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[52])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg1, msg0, 4);
    msg2 = _mm_add_epi32(msg2, tmp);
    msg2 = _mm_sha256msg2_epu32(msg2, msg1);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

    // Rounds 56-59
    msg = _mm_add_epi32(
      msg2, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[56])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    tmp = _mm_alignr_epi8(msg2, msg1, 4);
    msg3 = _mm_add_epi32(msg3, tmp);
    msg3 = _mm_sha256msg2_epu32(msg3, msg2);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

    // Rounds 60-63
    msg = _mm_add_epi32(
      msg3, _mm_loadu_si128(reinterpret_cast<const __m128i*>(&kK[60])));
    state1 = _mm_sha256rnds2_epu32(state1, state0, msg);
    msg = _mm_shuffle_epi32(msg, 0x0E);
    state0 = _mm_sha256rnds2_epu32(state0, state1, msg);

    // Add saved state
    state0 = _mm_add_epi32(state0, save_state0);
    state1 = _mm_add_epi32(state1, save_state1);
  }

  // Convert back: STATE0 = [A][B][E][F], STATE1 = [C][D][G][H]
  // Need: tmp0 = [D][C][B][A], tmp1 = [H][G][F][E] for storage
  // First get [C][D][A][B] and [G][H][E][F]
  tmp0 = _mm_unpackhi_epi64(state0, state1); // [C][D][A][B]
  tmp1 = _mm_unpacklo_epi64(state0, state1); // [G][H][E][F]

  // Swap pairs back: [C][D][A][B] -> [D][C][B][A], [G][H][E][F] -> [H][G][F][E]
  tmp0 = _mm_shuffle_epi32(tmp0, 0xB1); // D C B A
  tmp1 = _mm_shuffle_epi32(tmp1, 0xB1); // H G F E

  _mm_storeu_si128(reinterpret_cast<__m128i*>(&state_[0]), tmp0);
  _mm_storeu_si128(reinterpret_cast<__m128i*>(&state_[4]), tmp1);
}
#endif

auto ComputeSha256(std::span<const std::byte> data) noexcept -> Sha256Digest
{
  Sha256 hasher;
  hasher.Update(data);
  return hasher.Finalize();
}

auto IsAllZero(const Sha256Digest& digest) noexcept -> bool
{
  // Use a constant-time comparison to avoid timing attacks
  uint8_t acc = 0;
  for (const auto b : digest) {
    acc |= b;
  }
  return acc == 0;
}

auto ComputeFileSha256(const std::filesystem::path& path) -> Sha256Digest
{
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    throw std::runtime_error(
      "Failed to open file for SHA-256: " + path.string());
  }

  Sha256 hasher;

  // Use larger buffer for better I/O performance (256 KB)
  constexpr size_t kBufferSize = 256 * 1024;
  std::array<std::byte, kBufferSize> buffer = {};

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

} // namespace oxygen::base
