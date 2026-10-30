//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <benchmark/benchmark.h>

#include <Oxygen/Base/Crc32.h>
#include <Oxygen/Base/Hex.h>
#include <Oxygen/Base/Sha256.h>

namespace {

constexpr auto kSmallSize = int64_t { 64 };
constexpr auto kLargeSize = int64_t { 16 } * 1024 * 1024;
constexpr auto kRangeMultiplier = 8;
//! Smaller than the folding minimum, so every piece takes the table path.
constexpr auto kTablePieceSize = size_t { 48U };

auto MakeBytes(const int64_t size) -> std::vector<std::byte>
{
  constexpr auto kMultiplier = 1103515245U;
  constexpr auto kIncrement = 12345U;
  auto bytes = std::vector<std::byte>(static_cast<size_t>(size));
  auto state = 1U;
  for (auto& byte : bytes) {
    state = (state * kMultiplier) + kIncrement;
    byte = static_cast<std::byte>(state >> 16U);
  }
  return bytes;
}

auto SetThroughput(benchmark::State& state) -> void
{
  state.SetBytesProcessed(state.iterations() * state.range(0));
}

//! One call over the buffer: the fastest path the CPU allows.
auto BM_Crc32(benchmark::State& state) -> void
{
  const auto bytes = MakeBytes(state.range(0));
  state.SetLabel(
    oxygen::base::Crc32::HasHardwareSupport() ? "pclmul" : "slicing-by-8");
  for (auto _ : state) {
    benchmark::DoNotOptimize(oxygen::base::ComputeCrc32(bytes));
  }
  SetThroughput(state);
}

//! Pieces below the folding minimum: the portable slicing-by-8 path.
auto BM_Crc32SmallPieces(benchmark::State& state) -> void
{
  const auto bytes = MakeBytes(state.range(0));
  const auto all = std::span<const std::byte> { bytes };
  for (auto _ : state) {
    auto crc = oxygen::base::Crc32 {};
    for (auto offset = size_t { 0U }; offset < all.size();
      offset += kTablePieceSize) {
      crc.Update(all.subspan(offset).first(
        (std::min)(kTablePieceSize, all.size() - offset)));
    }
    benchmark::DoNotOptimize(crc.Finalize());
  }
  SetThroughput(state);
}

//! SHA-256 over the same buffers, for scale.
auto BM_Sha256(benchmark::State& state) -> void
{
  const auto bytes = MakeBytes(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(oxygen::base::ComputeSha256(bytes));
  }
  SetThroughput(state);
}

//! Spelling a buffer as hex text.
auto BM_ToHexString(benchmark::State& state) -> void
{
  const auto bytes = MakeBytes(state.range(0));
  for (auto _ : state) {
    benchmark::DoNotOptimize(oxygen::base::ToHexString(bytes));
  }
  SetThroughput(state);
}

//! Reading hex text back into bytes.
auto BM_FromHexString(benchmark::State& state) -> void
{
  const auto text = oxygen::base::ToHexString(MakeBytes(state.range(0)));
  auto out = std::vector<std::byte>(static_cast<size_t>(state.range(0)));
  for (auto _ : state) {
    benchmark::DoNotOptimize(oxygen::base::FromHexString(text, out));
  }
  SetThroughput(state);
}

} // namespace

BENCHMARK(BM_Crc32)
  ->RangeMultiplier(kRangeMultiplier)
  ->Range(kSmallSize, kLargeSize);
BENCHMARK(BM_Crc32SmallPieces)
  ->RangeMultiplier(kRangeMultiplier)
  ->Range(kSmallSize, kLargeSize);
BENCHMARK(BM_Sha256)
  ->RangeMultiplier(kRangeMultiplier)
  ->Range(kSmallSize, kLargeSize);
BENCHMARK(BM_ToHexString)->Arg(32)->Arg(4096);
BENCHMARK(BM_FromHexString)->Arg(32)->Arg(4096);
