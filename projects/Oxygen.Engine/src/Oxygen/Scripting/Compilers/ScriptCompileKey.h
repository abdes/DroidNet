//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Core/Meta/Scripting/ScriptCompileMode.h>
#include <Oxygen/Core/Types/AssetKey.h>
#include <Oxygen/Engine/Scripting/IScriptCompilationService.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

//! Identifies one compilation: the same script source compiled by the same
//! compiler in the same mode for the same platform shares its bytecode.
OXGN_SCRP_NDAPI auto ComputeScriptCompileKey(const data::AssetKey& asset_key,
  const ScriptSourceBlob& source,
  core::meta::scripting::ScriptCompileMode compile_mode)
  -> IScriptCompilationService::CompileKey;

} // namespace oxygen::scripting
