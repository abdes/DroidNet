//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/Base/ObserverPtr.h>
#include <Oxygen/Clap/Option.h>
#include <Oxygen/Cooker/Import/ImportRequest.h>
#include <Oxygen/Cooker/Import/SourceMounts.h>

namespace oxygen::content::import::tool {

class IMessageWriter;

//! The source mounts of a single import command.
struct SourceMountOptions final {
  //! `--mount <Name>=<dir>`, repeatable: absolute, or relative to the working
  //! directory.
  std::vector<std::string> mounts;
  //! `--mounts <file>`: applied above every `.mounts.json`.
  std::string mounts_file;
  //! `--root <dir>`: the project root; the working directory by default.
  std::string root;
};

//! The `--mount`, `--mounts` and `--root` options of a single import command.
[[nodiscard]] auto BuildSourceMountOptions(SourceMountOptions& options)
  -> std::shared_ptr<clap::Options>;

//! Builds the command's mount table; reports its diagnostics and returns
//! nothing when it is invalid.
[[nodiscard]] auto ResolveSourceMounts(const SourceMountOptions& options,
  observer_ptr<IMessageWriter> writer) -> std::optional<MountTable>;

//! Sets the request's recorded source: the mount path of its positional
//! source. Reports `import.source_outside_mount` and returns false when no
//! mount holds it.
[[nodiscard]] auto LocateRequestSource(ImportRequest& request,
  const MountTable& mounts, observer_ptr<IMessageWriter> writer) -> bool;

} // namespace oxygen::content::import::tool
