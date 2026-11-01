//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <filesystem>
#include <span>
#include <stdexcept>

#include "DemoShell/test/UiTestAssets.h"

#include <Oxygen/Cooker/testing/LooseCookedRoot.h>
#include <Oxygen/Cooker/testing/Materialize.h>
#include <Oxygen/Data/PakFormat_core.h>
#include <Oxygen/Serio/ByteReader.h>
#include <Oxygen/Vortex/test/Fixtures/TextureBinderPayloads.h>

namespace oxygen::examples::testing {
auto WriteUiTextureFixture(const std::filesystem::path& root) -> void
{
  const auto packed = vortex::testing::MakeCookedTexture1x1Rgba8Payload();
  const auto read = serio::CopyAt<data::pak::core::TextureResourceDesc>(
    std::as_bytes(std::span(packed)));
  if (!read) {
    throw std::runtime_error("the texture payload lacks its descriptor");
  }
  const auto descriptor = *read;
  const auto payload
    = std::as_bytes(std::span(packed))
        .subspan(descriptor.data_offset, descriptor.size_bytes);
  // Rows 1 and 2 hold the same 1x1 texture.
  auto loose = cooker::testing::LooseCookedRoot { "Ui" };
  static_cast<void>(loose.AddTexture(payload, descriptor));
  static_cast<void>(loose.AddTexture(payload, descriptor));
  cooker::testing::Materialize(loose.Files(), root);
}
} // namespace oxygen::examples::testing
