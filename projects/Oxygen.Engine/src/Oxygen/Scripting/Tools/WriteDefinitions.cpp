//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

//! Writes `oxygen.d.luau`, the Luau definitions of the scripting API, from the
//! default binding packs. The build runs it; the file is never edited.
/*!
 Usage: `Oxygen.Scripting.Definitions <output file>`. The file is rewritten
 only when its content changes, so dependent steps do not rerun.
*/

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <span>
#include <string>

#include <Oxygen/Scripting/Bindings/LuauDefinitions.h>

namespace {

auto ReadFile(const std::filesystem::path& path) -> std::string
{
  auto stream = std::ifstream(path, std::ios::binary);
  if (!stream) {
    return {};
  }
  return { std::istreambuf_iterator<char>(stream),
    std::istreambuf_iterator<char>() };
}

auto WriteDefinitions(const std::filesystem::path& output) -> int
{
  const auto packs = oxygen::scripting::bindings::CreateDefaultBindingPacks();
  const auto text = oxygen::scripting::bindings::GenerateLuauDefinitions(packs);
  if (ReadFile(output) == text) {
    return 0;
  }
  if (output.has_parent_path()) {
    std::filesystem::create_directories(output.parent_path());
  }
  auto stream = std::ofstream(output, std::ios::binary);
  stream << text;
  if (!stream) {
    std::cerr << "cannot write " << output.string() << "\n";
    return 1;
  }
  return 0;
}

} // namespace

auto main(const int argc, char** argv) -> int
{
  try {
    const auto args = std::span(argv, static_cast<std::size_t>(argc));
    if (args.size() != 2) {
      std::cerr << "usage: Oxygen.Scripting.Definitions <output file>\n";
      return 2;
    }
    return WriteDefinitions(std::filesystem::path(args[1]));
  } catch (...) {
    (void)std::fputs("cannot write the Luau definitions\n", stderr);
    return 1;
  }
}
