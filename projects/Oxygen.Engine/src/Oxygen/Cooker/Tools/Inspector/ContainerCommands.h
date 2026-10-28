//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <string>
#include <string_view>

#include <Oxygen/Clap/Command.h>
#include <Oxygen/Cooker/Tools/Common/ToolCommand.h>

namespace oxygen::content::inspection {

struct ValidateOptions {
  std::string cooked_root;
  bool quiet = false;
};

struct DumpOptions {
  std::string cooked_root;
  bool assets = false;
  bool files = false;
  bool show_digests = false;
};

//! `validate`: checks a loose cooked root, or a pak, for structural integrity.
class ValidateCommand final : public toolkit::ToolCommand {
public:
  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run(toolkit::ToolContext& context)
    -> toolkit::ToolResult override;

private:
  ValidateOptions options_;
};

//! `index`: dumps the asset entries and file records of the root's index.
class IndexCommand final : public toolkit::ToolCommand {
public:
  [[nodiscard]] auto Name() const -> std::string_view override;
  [[nodiscard]] auto BuildCommand() -> std::shared_ptr<clap::Command> override;
  [[nodiscard]] auto Run(toolkit::ToolContext& context)
    -> toolkit::ToolResult override;

private:
  DumpOptions options_;
};

} // namespace oxygen::content::inspection
