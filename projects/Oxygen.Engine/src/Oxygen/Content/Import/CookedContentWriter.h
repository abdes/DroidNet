//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cstdint>
#include <span>
#include <string_view>

#include <Oxygen/Data/AssetKey.h>
#include <Oxygen/Data/AssetType.h>
#include <Oxygen/Data/LooseCookedIndexFormat.h>

#include <Oxygen/Content/Import/ImportDiagnostics.h>

namespace oxygen::content::import {

//! Writer for runtime-compatible cooked content.
/*!
 This is the abstraction boundary between:
 - authoring-format import backends (FBX/glTF/...)
 - Oxygen's cooked content container emission (loose cooked today; PAK later)

 Backends generate *runtime-compatible cooked bytes* (descriptors and resources)
 and hand them to the writer. The writer is responsible for:
 - writing files to the cooked container,
 - emitting the container index/metadata,
 - applying engine-wide conventions (layout, hashing policy).

 This keeps backend code format-focused and keeps container policy in Oxygen.
*/
class CookedContentWriter {
public:
  virtual ~CookedContentWriter() = default;

  //! Record a diagnostic.
  virtual auto AddDiagnostic(ImportDiagnostic diag) -> void = 0;

  //! Write an asset descriptor and index it.
  /*!
   @param key Stable asset identity.
   @param asset_type Runtime loader dispatch type.
   @param virtual_path Virtual path (tooling/editor identity).
   @param descriptor_relpath Container-relative descriptor path.
   @param bytes Runtime-compatible descriptor bytes.
  */
  virtual auto WriteAssetDescriptor(const data::AssetKey& key,
    data::AssetType asset_type, std::string_view virtual_path,
    std::string_view descriptor_relpath, std::span<const std::byte> bytes)
    -> void
    = 0;

  //! Write an auxiliary cooked file and index it.
  /*!
   This is used for resource table/data blobs such as:
  - `Resources/textures.table`, `Resources/textures.data`
  - `Resources/buffers.table`, `Resources/buffers.data`
  */
  virtual auto WriteFile(data::loose_cooked::v1::FileKind kind,
    std::string_view relpath, std::span<const std::byte> bytes) -> void
    = 0;

  //! Register an externally-written file.
  /*!
   This is used when the data file was written directly (e.g., by
   append-only ResourceAppender) rather than through WriteFile().
   The file must already exist on disk at the given relpath.

   @param kind The file kind to register.
   @param relpath Container-relative file path.
  */
  virtual auto RegisterExternalFile(
    data::loose_cooked::v1::FileKind kind, std::string_view relpath) -> void
    = 0;

  //! Report summary counts for UI/telemetry.
  virtual auto OnMaterialsWritten(uint32_t count) -> void = 0;
  virtual auto OnGeometryWritten(uint32_t count) -> void = 0;
  virtual auto OnScenesWritten(uint32_t count) -> void = 0;
};

} // namespace oxygen::content::import
