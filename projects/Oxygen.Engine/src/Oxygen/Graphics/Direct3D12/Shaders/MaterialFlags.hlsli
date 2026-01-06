//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

// -----------------------------------------------------------------------------
// Material Flags
// -----------------------------------------------------------------------------

// Material opts out of all texture sampling (scalar fallbacks only).
static const uint MATERIAL_FLAG_NO_TEXTURE_SAMPLING = 1u;

// Material should be treated as double-sided (disable backface culling).
static const uint MATERIAL_FLAG_DOUBLE_SIDED = (1u << 1);

// Material uses alpha testing (cutout).
static const uint MATERIAL_FLAG_ALPHA_TEST = (1u << 2);

// Material is unlit.
static const uint MATERIAL_FLAG_UNLIT = (1u << 3);

// glTF ORM channel packing semantics.
static const uint MATERIAL_FLAG_GLTF_ORM_PACKED = (1u << 4);
