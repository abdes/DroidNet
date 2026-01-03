//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string>
#include <string_view>

namespace oxygen::content::import {

//! Common, container-agnostic layout conventions.
/*!
 Defines conventions for building virtual paths recorded into cooked indexes.

 These values describe a virtual namespace and common virtual directory names.
 They do not prescribe any particular on-disk layout.

 @note This type is intended to be used as a base for concrete container
  layouts (eg. loose cooked).
*/
struct Layout {
  //! Virtual mount root used to build virtual paths (e.g. "/.cooked").
  /*!
   This is a *logical namespace* prefix used when constructing virtual paths
   recorded in cooked indexes (and used for lookup/mounting).

   It does not affect where files are written on disk.

   @note This value should be an absolute virtual path prefix starting with
    '/'.
  */
  std::string virtual_mount_root = "/.cooked";

  //! Default virtual/physical directory name for material assets.
  static constexpr std::string_view kMaterialsDirName = "Materials";

  //! Default virtual/physical directory name for geometry assets.
  static constexpr std::string_view kGeometryDirName = "Geometry";

  //! Default virtual/physical directory name for resource blobs.
  static constexpr std::string_view kResourcesDirName = "Resources";

  //! Default virtual/physical directory name for scene assets.
  static constexpr std::string_view kScenesDirName = "Scenes";
};

} // namespace oxygen::content::import
