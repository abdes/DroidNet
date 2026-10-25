//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <Oxygen/Base/Sha256.h>
#include <Oxygen/Data/SceneIds.h>

namespace oxygen::content::import {

//! A node continuity entry of a model import record: the node with `anchor`
//! keeps the ID of the node with `continues`.
struct ModelNodeContinuity final {
  std::string anchor;
  std::string continues;

  auto operator==(const ModelNodeContinuity&) const -> bool = default;
};

//! A node an import of a model emitted: its anchor and its ID.
struct ModelPublishedNode final {
  std::string anchor;
  data::NodeId node_id;

  auto operator==(const ModelPublishedNode&) const -> bool = default;
};

//! The size and SHA-256 of a file an import read.
struct ModelSourceFile final {
  //! Relative to the folder of the model's primary source file; empty for the
  //! primary itself.
  std::string path;
  uint64_t size = 0;
  base::Sha256Digest sha256 {};

  auto operator==(const ModelSourceFile&) const -> bool = default;
};

//! What an import of a model read: its primary source file and every other
//! file it read whole.
struct ModelSourceFingerprint final {
  uint64_t size = 0;
  base::Sha256Digest sha256 {};
  //! The other files, sorted by path.
  std::vector<ModelSourceFile> files;

  auto operator==(const ModelSourceFingerprint&) const -> bool = default;
};

//! What earlier imports of a model established that its next import needs,
//! from its model import record.
struct ModelImportInputs final {
  std::vector<ModelNodeContinuity> node_continuity;
  //! The nodes the previous publication emitted.
  std::vector<ModelPublishedNode> published_nodes;
};

} // namespace oxygen::content::import
