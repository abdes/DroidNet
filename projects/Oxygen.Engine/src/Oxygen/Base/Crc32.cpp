//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <Oxygen/Base/Crc32.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#  include <cstring>

#  include <immintrin.h>
#  include <intrin.h>
#  define OXYGEN_CRC32_HAS_PCLMUL
#endif

namespace oxygen::base {

namespace {

  constexpr auto kPolynomial = uint32_t { 0xEDB88320U };
  constexpr auto kFinalXor = uint32_t { 0xFFFFFFFFU };
  constexpr auto kSliceWidth = size_t { 8U };
  constexpr auto kTableSize = size_t { 256U };
  constexpr auto kByteMask = uint32_t { 0xFFU };
  constexpr auto kByteBits = uint32_t { 8U };

  using Table = std::array<uint32_t, kTableSize>;

  //! Table `k` advances the CRC of one byte followed by `k` zero bytes, so the
  //! eight lookups of a slice combine into one step over eight bytes.
  constexpr auto kTables = [] consteval -> std::array<Table, kSliceWidth> {
    auto tables = std::array<Table, kSliceWidth> {};
    for (auto i = uint32_t { 0U }; i < kTableSize; ++i) {
      auto crc = i;
      for (auto bit = uint32_t { 0U }; bit < kByteBits; ++bit) {
        crc = (crc & 1U) != 0U ? (kPolynomial ^ (crc >> 1U)) : (crc >> 1U);
      }
      tables[0][i] = crc;
    }
    for (auto slice = size_t { 1U }; slice < kSliceWidth; ++slice) {
      for (auto i = size_t { 0U }; i < kTableSize; ++i) {
        const auto previous = tables[slice - 1U][i];
        tables[slice][i]
          = (previous >> kByteBits) ^ tables[0][previous & kByteMask];
      }
    }
    return tables;
  }();

  constexpr auto LoadLE32(const std::span<const std::byte, 4> bytes) noexcept
    -> uint32_t
  {
    return std::to_integer<uint32_t>(bytes[0])
      | (std::to_integer<uint32_t>(bytes[1]) << 8U)
      | (std::to_integer<uint32_t>(bytes[2]) << 16U)
      | (std::to_integer<uint32_t>(bytes[3]) << 24U);
  }

  constexpr auto Lookup(const size_t table, const uint32_t word,
    const uint32_t shift) noexcept -> uint32_t
  {
    return kTables[table][(word >> shift) & kByteMask];
  }

#ifdef OXYGEN_CRC32_HAS_PCLMUL
  //! Folding needs four 16-byte lanes to start.
  constexpr auto kFoldMinimum = size_t { 64U };
  constexpr auto kLaneSize = size_t { 16U };

  //! Bit-reflected folding constants for the IEEE polynomial (Gopal et al.,
  //! "Fast CRC Computation for Generic Polynomials Using PCLMULQDQ"): each
  //! pair is x^(D + 32) and x^(D - 32) mod P for a folding distance of D bits,
  //! bit-reflected and shifted left once. x^(512 +/- 32) folds over four
  //! lanes, x^(128 +/- 32) over one, x^64 reduces to 64 bits, then the
  //! polynomial and its Barrett constant reduce to 32.
  constexpr auto kFold4
    = std::array<uint64_t, 2> { 0x154442BD4U, 0x1C6E41596U };
  constexpr auto kFold1
    = std::array<uint64_t, 2> { 0x1751997D0U, 0x0CCAA009EU };
  constexpr auto kFold64 = uint64_t { 0x163CD6124U };
  constexpr auto kBarrett
    = std::array<uint64_t, 2> { 0x1DB710641U, 0x1F7011641U };

  [[nodiscard]] auto DetectPclmul() noexcept -> bool
  {
    auto cpu_info = std::array<int, 4> {};
    __cpuid(cpu_info.data(), 1);
    const auto ecx = static_cast<uint32_t>(cpu_info[2]);
    // PCLMULQDQ is ECX bit 1, SSE4.1 (for the final extract) bit 19.
    return (ecx & (1U << 1U)) != 0U && (ecx & (1U << 19U)) != 0U;
  }

  [[nodiscard]] auto HasPclmul() noexcept -> bool
  {
    static const bool kHasPclmul = DetectPclmul();
    return kHasPclmul;
  }

  [[nodiscard]] inline auto LoadLane(
    const std::span<const std::byte> bytes) noexcept -> __m128i
  {
    auto value = __m128i {};
    std::memcpy(&value, bytes.data(), sizeof(value));
    return value;
  }

  //! A pair of folding constants, low quadword first. A type of its own, so
  //! it cannot be passed where a data lane is expected.
  struct FoldConstants final {
    __m128i pair;
  };

  [[nodiscard]] inline auto LoadConstants(
    const std::array<uint64_t, 2>& constants) noexcept -> FoldConstants
  {
    return {
      .pair = _mm_set_epi64x(
        static_cast<int64_t>(constants[1]), static_cast<int64_t>(constants[0])),
    };
  }

  //! `lane` times x^distance, plus `next`: one carry-less folding step.
  [[nodiscard]] inline auto Fold(const __m128i lane,
    const FoldConstants constants, const __m128i next) noexcept -> __m128i
  {
    const auto low = _mm_clmulepi64_si128(lane, constants.pair, 0x00);
    const auto high = _mm_clmulepi64_si128(lane, constants.pair, 0x11);
    return _mm_xor_si128(_mm_xor_si128(high, low), next);
  }

  //! Folds four consecutive lanes and the rest of `data`, a multiple of 16
  //! bytes, down to the reflected 32-bit CRC state.
  [[nodiscard]] auto FoldLanes(std::array<__m128i, 4> lanes,
    std::span<const std::byte> data) noexcept -> uint32_t
  {
    auto& [x1, x2, x3, x4] = lanes;
    const auto fold4 = LoadConstants(kFold4);
    while (data.size() >= kFoldMinimum) {
      x1 = Fold(x1, fold4, LoadLane(data));
      x2 = Fold(x2, fold4, LoadLane(data.subspan(kLaneSize)));
      x3 = Fold(x3, fold4, LoadLane(data.subspan(2U * kLaneSize)));
      x4 = Fold(x4, fold4, LoadLane(data.subspan(3U * kLaneSize)));
      data = data.subspan(kFoldMinimum);
    }

    const auto fold1 = LoadConstants(kFold1);
    x1 = Fold(x1, fold1, x2);
    x1 = Fold(x1, fold1, x3);
    x1 = Fold(x1, fold1, x4);
    while (data.size() >= kLaneSize) {
      x1 = Fold(x1, fold1, LoadLane(data));
      data = data.subspan(kLaneSize);
    }

    // 128 bits to 64: fold the low half onto the high half.
    const auto mask32 = _mm_setr_epi32(-1, 0, -1, 0);
    x1 = _mm_xor_si128(
      _mm_srli_si128(x1, 8), _mm_clmulepi64_si128(x1, fold1.pair, 0x10));
    // 64 bits to 32 plus a remainder: fold the low word by x^64.
    const auto fold64 = _mm_set_epi64x(0, static_cast<int64_t>(kFold64));
    x1 = _mm_xor_si128(_mm_srli_si128(x1, 4),
      _mm_clmulepi64_si128(_mm_and_si128(x1, mask32), fold64, 0x00));

    // Barrett reduction to the 32-bit remainder.
    const auto barrett = LoadConstants(kBarrett);
    auto t
      = _mm_clmulepi64_si128(_mm_and_si128(x1, mask32), barrett.pair, 0x10);
    t = _mm_clmulepi64_si128(_mm_and_si128(t, mask32), barrett.pair, 0x00);
    return static_cast<uint32_t>(_mm_extract_epi32(_mm_xor_si128(x1, t), 1));
  }

  //! Continues the reflected CRC state over `data`, whose size is a multiple
  //! of 16 and at least 64 bytes.
  [[nodiscard]] auto UpdatePclmul(const std::span<const std::byte> data,
    const uint32_t state) noexcept -> uint32_t
  {
    return FoldLanes(
      {
        _mm_xor_si128(
          LoadLane(data), _mm_cvtsi32_si128(static_cast<int>(state))),
        LoadLane(data.subspan(kLaneSize)),
        LoadLane(data.subspan(2U * kLaneSize)),
        LoadLane(data.subspan(3U * kLaneSize)),
      },
      data.subspan(kFoldMinimum));
  }

  //! Four 64-byte registers of four lanes each start the wide fold.
  constexpr auto kWideFoldMinimum = size_t { 256U };
  constexpr auto kWideRegisterSize = size_t { 64U };
  //! x^(2048 +/- 32): folds each register over the three that follow it.
  constexpr auto kFold16
    = std::array<uint64_t, 2> { 0x11542778AU, 0x1322D1430U };
  //! `vpternlog` truth table for a three-way exclusive or.
  constexpr auto kXor3 = 0x96;

  [[nodiscard]] auto DetectVpclmul() noexcept -> bool
  {
    auto cpu_info = std::array<int, 4> {};
    __cpuid(cpu_info.data(), 0);
    if (cpu_info[0] < 7) {
      return false;
    }
    __cpuid(cpu_info.data(), 1);
    // OSXSAVE (ECX bit 27): the OS reports which register state it saves.
    if ((static_cast<uint32_t>(cpu_info[2]) & (1U << 27U)) == 0U) {
      return false;
    }
    // XMM, YMM, opmask and both ZMM halves (XCR0 bits 1, 2, 5, 6 and 7).
    constexpr auto kAvx512State = uint64_t { 0xE6U };
    if ((_xgetbv(0) & kAvx512State) != kAvx512State) {
      return false;
    }
    __cpuidex(cpu_info.data(), 7, 0);
    // AVX512F is EBX bit 16, VPCLMULQDQ is ECX bit 10.
    return (static_cast<uint32_t>(cpu_info[1]) & (1U << 16U)) != 0U
      && (static_cast<uint32_t>(cpu_info[2]) & (1U << 10U)) != 0U;
  }

  [[nodiscard]] auto HasVpclmul() noexcept -> bool
  {
    static const bool kHasVpclmul = HasPclmul() && DetectVpclmul();
    return kHasVpclmul;
  }

  [[nodiscard]] inline auto LoadWide(
    const std::span<const std::byte> bytes) noexcept -> __m512i
  {
    return _mm512_loadu_si512(bytes.data());
  }

  //! A pair of folding constants repeated in all four lanes of a register.
  struct WideFoldConstants final {
    __m512i pairs;
  };

  [[nodiscard]] inline auto Broadcast(const FoldConstants constants) noexcept
    -> WideFoldConstants
  {
    return { .pairs = _mm512_broadcast_i32x4(constants.pair) };
  }

  //! Four lanes at once: `lanes` times x^distance, plus `next`.
  [[nodiscard]] inline auto FoldWide(const __m512i lanes,
    const WideFoldConstants constants, const __m512i next) noexcept -> __m512i
  {
    return _mm512_ternarylogic_epi64(
      _mm512_clmulepi64_epi128(lanes, constants.pairs, 0x00),
      _mm512_clmulepi64_epi128(lanes, constants.pairs, 0x11), next, kXor3);
  }

  //! Continues the reflected CRC state over `data`, whose size is a multiple
  //! of 16 and at least 256 bytes, sixteen lanes at a time.
  [[nodiscard]] auto UpdateVpclmul(
    std::span<const std::byte> data, const uint32_t state) noexcept -> uint32_t
  {
    auto z1 = _mm512_xor_si512(LoadWide(data),
      _mm512_zextsi128_si512(_mm_cvtsi32_si128(static_cast<int>(state))));
    auto z2 = LoadWide(data.subspan(kWideRegisterSize));
    auto z3 = LoadWide(data.subspan(2U * kWideRegisterSize));
    auto z4 = LoadWide(data.subspan(3U * kWideRegisterSize));
    data = data.subspan(kWideFoldMinimum);

    const auto fold16 = Broadcast(LoadConstants(kFold16));
    while (data.size() >= kWideFoldMinimum) {
      z1 = FoldWide(z1, fold16, LoadWide(data));
      z2 = FoldWide(z2, fold16, LoadWide(data.subspan(kWideRegisterSize)));
      z3 = FoldWide(z3, fold16, LoadWide(data.subspan(2U * kWideRegisterSize)));
      z4 = FoldWide(z4, fold16, LoadWide(data.subspan(3U * kWideRegisterSize)));
      data = data.subspan(kWideFoldMinimum);
    }

    // Each lane folded over four lanes lands on its peer in the next register.
    const auto fold4 = Broadcast(LoadConstants(kFold4));
    z1 = FoldWide(z1, fold4, z2);
    z1 = FoldWide(z1, fold4, z3);
    z1 = FoldWide(z1, fold4, z4);
    return FoldLanes(
      {
        _mm512_extracti32x4_epi32(z1, 0),
        _mm512_extracti32x4_epi32(z1, 1),
        _mm512_extracti32x4_epi32(z1, 2),
        _mm512_extracti32x4_epi32(z1, 3),
      },
      data);
  }
#endif

} // namespace

auto Crc32::HasHardwareSupport() noexcept -> bool
{
#ifdef OXYGEN_CRC32_HAS_PCLMUL
  return HasPclmul();
#else
  return false;
#endif
}

auto Crc32::Update(std::span<const std::byte> data) noexcept -> void
{
  auto crc = state_;
#ifdef OXYGEN_CRC32_HAS_PCLMUL
  if (data.size() >= kFoldMinimum && HasPclmul()) {
    const auto folded = data.size() & ~(kLaneSize - 1U);
    crc = folded >= kWideFoldMinimum && HasVpclmul()
      ? UpdateVpclmul(data.first(folded), crc)
      : UpdatePclmul(data.first(folded), crc);
    data = data.subspan(folded);
  }
#endif
  while (data.size() >= kSliceWidth) {
    const auto low = LoadLE32(data.first<4>()) ^ crc;
    const auto high = LoadLE32(data.subspan<4, 4>());
    crc = Lookup(7U, low, 0U) ^ Lookup(6U, low, 8U) ^ Lookup(5U, low, 16U)
      ^ Lookup(4U, low, 24U) ^ Lookup(3U, high, 0U) ^ Lookup(2U, high, 8U)
      ^ Lookup(1U, high, 16U) ^ Lookup(0U, high, 24U);
    data = data.subspan(kSliceWidth);
  }
  for (const auto byte : data) {
    crc = Lookup(0U, crc ^ std::to_integer<uint32_t>(byte), 0U)
      ^ (crc >> kByteBits);
  }
  state_ = crc;
}

auto Crc32::Finalize() const noexcept -> uint32_t { return state_ ^ kFinalXor; }

auto ComputeCrc32(const std::span<const std::byte> data) noexcept -> uint32_t
{
  auto crc = Crc32 {};
  crc.Update(data);
  return crc.Finalize();
}

} // namespace oxygen::base
