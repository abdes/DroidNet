//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::pakdump {

//! The default command: dumps and inspects one PAK archive.
class PakDumpCommand final : public toolkit::ToolCommand {
public:
  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run(toolkit::ToolContext& context)
    -> toolkit::ToolResult override;

private:
  std::string pakfile_;
  //! The paks the dumped one depends on, mounted below it in this order.
  std::vector<std::filesystem::path> mounts_;
  bool no_header_ = false;
  bool no_footer_ = false;
  bool no_directory_ = false;
  bool no_resources_ = false;
  bool show_data_ = false;
  bool hex_dump_assets_ = false;
  bool verbose_ = false;
  size_t max_data_bytes_ = 256;
};

} // namespace oxygen::content::pakdump
