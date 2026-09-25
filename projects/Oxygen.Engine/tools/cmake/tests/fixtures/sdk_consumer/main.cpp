// Distributed under the 3-Clause BSD License. See accompanying file LICENSE.
// SPDX-License-Identifier: BSD-3-Clause

#include <iostream>

#include <fmt/format.h>
#include <glm/vec3.hpp>
#include <glm/geometric.hpp>

#include <Oxygen/Core/Version.h>

auto main() -> int
{
  const glm::vec3 position { 1.0F, 2.0F, 3.0F };
  const auto squared_length = glm::dot(position, position);
  const auto version = oxygen::version::NameVersion();
  std::cout << fmt::format("{}; bundled GLM dot product = {}\n", version, squared_length);
  return !version.empty() && squared_length == 14.0F ? 0 : 1;
}
