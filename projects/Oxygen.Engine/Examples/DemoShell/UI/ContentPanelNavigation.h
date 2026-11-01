//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>

namespace oxygen::examples::ui {

//! The tabs of the content panel, in order.
enum class ContentTab : uint8_t {
  kScenes,
  kMounts,
  kImport,
  kLog,
};

//! What one part of the panel asks another to show: the Log's "Go to the
//! mount" opens the Mounts tab on a source, the status strip's counts open
//! the Log. The panel takes the tab request; the Mounts tab takes the source.
struct ContentPanelNavigation final {
  std::optional<ContentTab> open_tab;
  std::optional<std::filesystem::path> select_source;
};

} // namespace oxygen::examples::ui
