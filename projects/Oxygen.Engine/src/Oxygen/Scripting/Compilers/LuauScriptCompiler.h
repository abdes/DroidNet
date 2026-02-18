//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Data/PakFormat.h>
#include <Oxygen/Engine/Scripting/IScriptCompiler.h>
#include <Oxygen/Scripting/api_export.h>

namespace oxygen::scripting {

class LuauScriptCompiler final : public IScriptCompiler {
public:
  OXGN_SCRP_NDAPI auto Language() const noexcept
    -> data::pak::ScriptLanguage override;

  OXGN_SCRP_NDAPI auto Compile(ScriptSourceBlob source, CompileMode mode) const
    -> ScriptCompileResult override;
};

} // namespace oxygen::scripting
