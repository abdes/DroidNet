//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <string_view>

#include <Oxygen/Data/LooseCookedIndexFormat.h>

//! Where the loose writer places a cooked root's files.
/*!
 The layout is a detail of writing a loose root, not a setting. Every
 descriptor lives at the path of its virtual path under the cooked root (the
 descriptor of `/Mount/X/Y.ext` is `X/Y.ext`); these constants place only the
 index and the shared resource tables and data. Readers never use them: they
 follow the paths the index records, so roots written with another layout keep
 loading.

 All paths are relative to the cooked root and use forward slashes.
*/
namespace oxygen::content::import::internal::loose_layout {

//! Index file at the cooked root: the format's own name.
using data::loose_cooked::kIndexFileName;

//! Folder of the buffer, texture and script tables and data.
inline constexpr std::string_view kResourcesDir = "Resources";

inline constexpr std::string_view kBuffersTable = "Resources/buffers.table";
inline constexpr std::string_view kBuffersData = "Resources/buffers.data";
inline constexpr std::string_view kTexturesTable = "Resources/textures.table";
inline constexpr std::string_view kTexturesData = "Resources/textures.data";
inline constexpr std::string_view kScriptsTable = "Resources/scripts.table";
inline constexpr std::string_view kScriptsData = "Resources/scripts.data";

//! Folder of the physics resource table and data.
inline constexpr std::string_view kPhysicsResourcesDir = "Physics/Resources";

inline constexpr std::string_view kPhysicsTable
  = "Physics/Resources/physics.table";
inline constexpr std::string_view kPhysicsData
  = "Physics/Resources/physics.data";

} // namespace oxygen::content::import::internal::loose_layout
