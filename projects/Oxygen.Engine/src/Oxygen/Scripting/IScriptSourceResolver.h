//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <variant>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/Data/ScriptAsset.h>
#include <Oxygen/Data/ScriptResource.h>
#include <Oxygen/Engine/Scripting/ScriptBytecodeBlob.h>
#include <Oxygen/Engine/Scripting/ScriptSourceBlob.h>

namespace oxygen::scripting {

using ResolvedScriptBlob = std::variant<ScriptSourceBlob, ScriptBytecodeBlob>;

class IScriptSourceResolver {
public:
  struct ResolveRequest {
    using LoadScriptResourceFn
      = std::function<std::shared_ptr<const data::ScriptResource>(uint32_t)>;
    using ResourceOriginMapperFn
      = std::function<std::optional<ScriptBlobOrigin>(uint32_t)>;

    std::reference_wrapper<const data::ScriptAsset> asset;
    LoadScriptResourceFn load_script_resource;
    ResourceOriginMapperFn map_resource_origin;
  };

  struct ResolveResult {
    bool ok { false };
    std::optional<ResolvedScriptBlob> blob;
    std::string error_message;
  };

  IScriptSourceResolver() = default;
  virtual ~IScriptSourceResolver() = default;

  OXYGEN_MAKE_NON_COPYABLE(IScriptSourceResolver)
  OXYGEN_MAKE_NON_MOVABLE(IScriptSourceResolver)

  [[nodiscard]] virtual auto Resolve(const ResolveRequest& request) const
    -> ResolveResult
    = 0;
};

} // namespace oxygen::scripting
