//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <array>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <span>
#include <stdexcept>
#include <vector>

#include "DemoShell/test/UiTestAssets.h"

#include <Oxygen/Cooker/Import/Internal/LooseCookedWriter.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/NativeFile.h>
#include <Oxygen/Vortex/test/Fixtures/TextureBinderPayloads.h>

namespace oxygen::examples::testing {
auto WriteUiTextureFixture(const std::filesystem::path& root) -> void
{
  std::filesystem::create_directories(root);
  const auto packed = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  data::pak::core::TextureResourceDesc descriptor {};
  std::memcpy(&descriptor, packed.data(), sizeof(descriptor));
  const auto payload
    = std::as_bytes(std::span(packed))
        .subspan(descriptor.data_offset, descriptor.size_bytes);
  std::array<data::pak::core::TextureResourceDesc, 3> table {};
  std::vector<std::byte> bytes;
  for (size_t index = 1; index < table.size(); ++index) {
    descriptor.data_offset = bytes.size();
    table.at(index) = descriptor;
    bytes.insert(bytes.end(), payload.begin(), payload.end());
  }
  const auto write
    = [&root](const char* name, std::span<const std::byte> data) -> void {
    auto file = serio::NativeFile::Open(root / name,
      {
        .access = serio::Access::kWrite,
        .creation = serio::Creation::kCreateAlways,
      });
    if (!file || !file->WriteAt(serio::ByteOffset { 0 }, data)) {
      throw std::runtime_error("Cannot write UI texture fixture");
    }
  };
  write("textures.table", std::as_bytes(std::span(table)));
  write("textures.data", bytes);
  content::import::LooseCookedWriter writer(root);
  writer.RegisterExternalFile(
    data::loose_cooked::FileKind::kTexturesTable, "textures.table");
  writer.RegisterExternalFile(
    data::loose_cooked::FileKind::kTexturesData, "textures.data");
  static_cast<void>(writer.Finish());
}
} // namespace oxygen::examples::testing
