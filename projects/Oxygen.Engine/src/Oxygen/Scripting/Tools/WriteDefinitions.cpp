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

#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <span>

#include <Oxygen/Scripting/Bindings/LuauDefinitions.h>
#include <Oxygen/Serio/AtomicFile.h>
#include <Oxygen/Serio/ByteBuffer.h>
#include <Oxygen/Serio/Types.h>

namespace {

//! Whether the file `path` holds exactly `bytes`. A larger file differs, so
//! no more than `bytes` is read.
auto Holds(const std::filesystem::path& path,
  const std::span<const std::byte> bytes) -> bool
{
  const auto current = oxygen::serio::ReadPublishedFile(
    path, oxygen::serio::ByteCount { bytes.size() });
  return current && std::ranges::equal(current->Bytes(), bytes);
}

auto WriteDefinitions(const std::filesystem::path& output) -> int
{
  const auto packs = oxygen::scripting::bindings::CreateDefaultBindingPacks();
  const auto text = oxygen::scripting::bindings::GenerateLuauDefinitions(packs);
  const auto bytes = std::as_bytes(std::span(text));
  if (Holds(output, bytes)) {
    return 0;
  }
  if (output.has_parent_path()) {
    std::filesystem::create_directories(output.parent_path());
  }
  // Every build regenerates the file, so the OS cache is durable enough.
  const auto written = oxygen::serio::WriteFileAtomically(
    output, bytes, oxygen::serio::Durability::kBuffered);
  if (!written) {
    std::cerr << "cannot write " << output.string() << ": "
              << written.error().message() << "\n";
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
