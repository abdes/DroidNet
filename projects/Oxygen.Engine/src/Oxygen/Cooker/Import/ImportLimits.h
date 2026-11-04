//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <limits>

#include <Oxygen/Serio/Types.h>

namespace oxygen::content::import {

//! The most bytes the import reads whole from a source file by default.
/*!
 A provisional product limit, not a measured memory budget: it admits large
 binary sources and stays within the image decoders' `int` input size. A
 larger source is refused before anything is allocated, with its own
 diagnostic. Import configuration may set another limit.
*/
inline constexpr serio::ByteCount kDefaultMaxSourceBytes {
  static_cast<uint64_t>(std::numeric_limits<int32_t>::max()),
};

//! The deepest node hierarchy a model import accepts: nodes on one path from a
//! root, the root included. Each level is a stack frame of the import and a
//! copy of the node's anchor path, so a file cannot choose how deep they go.
inline constexpr uint32_t kMaxModelNodeDepth = 256U;

//! The most bytes the Cooker reads whole from a metadata file written for
//! it: a manifest, a mounts file, a tool request, a model import record or
//! a buffer sidecar.
inline constexpr serio::ByteCount kMaxImportMetadataBytes {
  uint64_t { 64 } * 1024 * 1024,
};

} // namespace oxygen::content::import
