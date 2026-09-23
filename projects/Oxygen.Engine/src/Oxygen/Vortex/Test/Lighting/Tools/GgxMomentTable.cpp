//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <syncstream>
#include <system_error>
#include <thread>
#include <vector>

#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <nlohmann/json_fwd.hpp>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/GgxMoments.h>
#include <Oxygen/Vortex/Test/Lighting/Reference/ReferenceQuadrature.h>

namespace {

namespace reference = oxygen::vortex::testing::reference;
constexpr double kMinimumRoughness = 0.045;
constexpr double kReferenceRefinementTolerance = 1.0e-8;
constexpr std::uint32_t kModelRevision = 1U;
constexpr std::uint32_t kMaximumDimension = 4097U;
constexpr std::uint32_t kMaximumWorkers = 64U;
using MomentPair = std::array<float, 2>;

struct TableShape {
  std::uint32_t view_nodes;
  std::uint32_t roughness_nodes;
};

struct Row {
  std::vector<MomentPair> samples;
  MomentPair mean {};
  double maximum_refinement_change { 0.0 };
  double maximum_domain_projection { 0.0 };
  std::uint64_t evaluations { 0U };
};

auto ParsePositive(const std::string_view text) -> std::uint32_t
{
  std::uint32_t value = 0;
  const auto* const first = std::to_address(text.begin());
  const auto* const last = std::to_address(text.end());
  const auto result = std::from_chars(first, last, value);
  if (result.ec != std::errc {} || result.ptr != last || value == 0U) {
    throw std::invalid_argument(
      "Expected a positive integer: " + std::string(text));
  }
  return value;
}

auto HexDigest(const oxygen::base::Sha256Digest& digest) -> std::string
{
  auto text = std::string {};
  for (const auto byte : digest) {
    text += fmt::format("{:02x}", byte);
  }
  return text;
}

// Integrate the actual piecewise-linear table, not a separately fitted mean.
// With s=sqrt(mu), the hemispherical measure 2*mu*dmu becomes 4*s^3*ds.
auto IntegrateTableRow(const std::vector<MomentPair>& samples) -> MomentPair
{
  const auto step = 1.0 / static_cast<double>(samples.size() - 1U);
  auto loss = reference::detail::Sum {};
  auto bias = reference::detail::Sum {};
  for (std::size_t index = 1U; index < samples.size(); ++index) {
    const auto a = static_cast<double>(index - 1U) * step;
    const auto a2h2 = a * a * step * step;
    const auto a3h = a * a * a * step;
    const auto ah3 = a * step * step * step;
    const auto h4 = step * step * step * step;
    // Positive polynomial weights avoid subtracting adjacent powers near one.
    const auto left = (2.0 * a3h) + (2.0 * a2h2) + ah3 + (h4 / 5.0);
    const auto right
      = (2.0 * a3h) + (4.0 * a2h2) + (3.0 * ah3) + (4.0 * h4 / 5.0);
    const auto& previous = samples.at(index - 1U);
    const auto& current = samples.at(index);
    loss.Add((left * previous.at(0)) + (right * current.at(0)));
    bias.Add((left * previous.at(1)) + (right * current.at(1)));
  }
  return { static_cast<float>(loss.value), static_cast<float>(bias.value) };
}

auto GenerateRow(const TableShape shape, const std::uint32_t row_index) -> Row
{
  const auto roughness = kMinimumRoughness
    + ((1.0 - kMinimumRoughness) * row_index / (shape.roughness_nodes - 1U));
  auto row = Row {};
  row.samples.reserve(shape.view_nodes);
  for (std::uint32_t column = 0; column < shape.view_nodes; ++column) {
    const auto s = static_cast<double>(column) / (shape.view_nodes - 1U);
    const auto moment = reference::IntegrateGgxMoments(
      reference::PerceptualRoughness { roughness },
      reference::ViewCosine { s * s },
      { .refinement_tolerance = kReferenceRefinementTolerance });
    if (!moment) {
      throw std::runtime_error(
        fmt::format("Reference did not converge at row {}, column {} (r={}, "
                    "mu={}, order={})",
          row_index, column, roughness, s * s,
          moment.error().last_estimate.order));
    }
    const auto energy = moment->directional_albedo;
    const auto bias = moment->schlick_moment;
    // The independently certified grazing limit is exactly E(0)=1.
    const auto bounded_energy
      = column == 0U ? 1.0 : std::clamp(energy, 0.0, 1.0);
    const auto bounded_bias = std::clamp(bias, 0.0, bounded_energy);
    const auto projection = std::max(
      std::abs(energy - bounded_energy), std::abs(bias - bounded_bias));
    if (!std::isfinite(energy) || !std::isfinite(bias)
      || projection > kReferenceRefinementTolerance) {
      throw std::runtime_error(
        fmt::format("Reference violates the moment domain at row {}, column {}",
          row_index, column));
    }
    row.samples.push_back({
      static_cast<float>(1.0 - bounded_energy),
      static_cast<float>(bounded_bias),
    });
    row.maximum_refinement_change = std::max(
      row.maximum_refinement_change, moment->estimated_absolute_change);
    row.maximum_domain_projection
      = std::max(row.maximum_domain_projection, projection);
    row.evaluations += moment->evaluations;
  }
  row.mean = IntegrateTableRow(row.samples);
  return row;
}

auto Generate(const TableShape shape, const std::uint32_t worker_count)
  -> std::vector<Row>
{
  auto rows = std::vector<Row>(shape.roughness_nodes);
  auto next_row = std::atomic<std::uint32_t> { 0U };
  auto failed = std::atomic_bool { false };
  auto failure_mutex = std::mutex {};
  auto failure = std::string {};
  {
    auto workers = std::vector<std::jthread> {};
    workers.reserve(worker_count);
    for (std::uint32_t worker = 0; worker < worker_count; ++worker) {
      workers.emplace_back([&] -> void {
        try {
          while (!failed.load()) {
            const auto index = next_row.fetch_add(1U);
            if (index >= shape.roughness_nodes) {
              break;
            }
            rows.at(index) = GenerateRow(shape, index);
            std::osyncstream(std::cout) << "completed row " << index << '/'
                                        << shape.roughness_nodes << '\n';
          }
        } catch (const std::exception& error) {
          if (!failed.exchange(true)) {
            const auto lock = std::scoped_lock(failure_mutex);
            failure = error.what();
          }
        }
      });
    }
  }
  if (failed) {
    throw std::runtime_error(failure);
  }
  return rows;
}

auto WriteCandidate(const std::filesystem::path& output, const TableShape shape,
  const std::vector<Row>& rows,
  const oxygen::base::Sha256Digest& generator_hash) -> void
{
  static_assert(std::endian::native == std::endian::little);
  auto moments = std::vector<MomentPair> {};
  auto means = std::vector<MomentPair> {};
  auto maximum_change = 0.0;
  auto maximum_projection = 0.0;
  auto evaluations = std::uint64_t { 0U };
  for (const auto& row : rows) {
    moments.insert(moments.end(), row.samples.begin(), row.samples.end());
    means.push_back(row.mean);
    maximum_change = std::max(maximum_change, row.maximum_refinement_change);
    maximum_projection
      = std::max(maximum_projection, row.maximum_domain_projection);
    evaluations += row.evaluations;
  }
  auto hash = oxygen::base::Sha256 {};
  hash.Update(std::as_bytes(std::span(moments)));
  hash.Update(std::as_bytes(std::span(means)));
  const auto document = nlohmann::json {
    { "schema", 1U },
    { "model_revision", kModelRevision },
    {
      "qualification_status",
      "candidate: interpolation and physical qualification required",
    },
    { "generator_sha256", HexDigest(generator_hash) },
    { "payload_sha256", HexDigest(hash.Finalize()) },
    {
      "payload_encoding",
      "little-endian binary32 pairs: all moment rows, then means",
    },
    { "view_nodes", shape.view_nodes },
    { "roughness_nodes", shape.roughness_nodes },
    { "roughness_minimum", kMinimumRoughness },
    { "view_mapping", "sqrt(mu), endpoint nodes" },
    { "roughness_mapping", "linear effective roughness, endpoint nodes" },
    {
      "mean_rule",
      "exact integral of the piecewise-linear float32 directional table",
    },
    { "reference_refinement_tolerance", kReferenceRefinementTolerance },
    { "maximum_reference_refinement_change", maximum_change },
    { "maximum_domain_projection", maximum_projection },
    { "reference_evaluations", evaluations },
    { "loss_and_bias", moments },
    { "mean_loss_and_bias", means },
  };
  auto stream = std::ofstream(output, std::ios::binary);
  if (!stream.is_open()) {
    throw std::runtime_error(
      "Cannot create moment candidate: " + output.string());
  }
  stream << document.dump() << '\n';
  stream.close();
  if (!stream) {
    throw std::runtime_error(
      "Cannot write moment candidate: " + output.string());
  }
}

auto Run(const std::span<char*> arguments) -> int
{
  const auto args
    = std::vector<std::string_view>(arguments.begin(), arguments.end());
  constexpr std::size_t kArgumentCount = 5U;
  if (args.size() != kArgumentCount) {
    std::cerr
      << "Usage: Oxygen.Vortex.GgxMomentTable <output.json> <view-nodes> "
         "<roughness-nodes> <workers>\n";
    return 1;
  }
  const auto shape = TableShape {
    .view_nodes = ParsePositive(args.at(2)),
    .roughness_nodes = ParsePositive(args.at(3)),
  };
  const auto workers = ParsePositive(args.at(4));
  if (shape.view_nodes < 2U || shape.roughness_nodes < 2U
    || shape.view_nodes > kMaximumDimension
    || shape.roughness_nodes > kMaximumDimension || workers > kMaximumWorkers) {
    throw std::invalid_argument("Dimensions must be 2..4097 and workers 1..64");
  }
  const auto generator_hash = oxygen::base::ComputeFileSha256(args.front());
  const auto rows = Generate(shape, workers);
  WriteCandidate(args.at(1), shape, rows, generator_hash);
  return 0;
}

} // namespace

auto main(const int argc, char** argv) -> int
{
  try {
    return Run(std::span(argv, static_cast<std::size_t>(argc)));
  } catch (const std::exception& error) {
    std::fputs(error.what(), stderr);
    std::fputc('\n', stderr);
    return 1;
  } catch (...) {
    std::fputs("Unexpected moment generation failure\n", stderr);
    return 1;
  }
}
