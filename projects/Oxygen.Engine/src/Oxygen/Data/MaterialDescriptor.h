//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <span>

#include <Oxygen/Data/PakFormat_render.h>
#include <Oxygen/Data/ShaderReference.h>
#include <Oxygen/Data/api_export.h>

namespace oxygen::data {

//! Checks the values of a cooked material descriptor and that its shader
//! references are the stages it declares.
/*!
 The domain is one the format defines other than `kUnknown`; flags and shader
 stages set only the bits the format defines; `shaders` holds one reference
 per stage of `desc.shader_stages`, in ascending stage order; emissive factors
 are finite and within [0, kMaxMaterialEmissiveFactor]. Each reference checks
 its own contract when it is built (ShaderReference).

 @throw CookedOpenError `kInvalidValue` for an unknown domain, flag or stage,
 or an emissive factor out of range; `kInconsistent` when the references are
 not the declared stages.
*/
OXGN_DATA_API auto CheckMaterialDescriptor(
  const pak::render::MaterialAssetDesc& desc,
  std::span<const ShaderReference> shaders) -> void;

} // namespace oxygen::data
