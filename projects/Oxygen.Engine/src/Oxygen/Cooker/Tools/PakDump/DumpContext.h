//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//
#pragma once

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ostream>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

struct DumpContext {
  // Options
  bool show_header = true;
  bool show_footer = true;
  bool show_directory = true;
  bool show_resources = true;
  bool show_resource_data = false;
  bool show_asset_descriptors = false;
  bool verbose = false;
  size_t max_data_bytes = 256;

  // Shared state
  std::filesystem::path pak_path;

  //! The tool's injected output streams; the dumpers write nothing else.
  oxygen::content::toolkit::ToolStreams streams;

  [[nodiscard]] auto Out() const -> std::ostream& { return *streams.out; }
  [[nodiscard]] auto Err() const -> std::ostream& { return *streams.err; }
};
