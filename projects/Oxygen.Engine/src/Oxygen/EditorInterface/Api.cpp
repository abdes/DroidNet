//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <Oxygen/Base/Logging.h>
#include <Oxygen/EditorInterface/Api.h>
#include <Oxygen/Scene/Scene.h>

using oxygen::scene::Scene;

auto CreateScene(const char* name) -> bool
{
  // Scene name must be a non-null, null terminated string that is not be
  // empty
  CHECK_NOTNULL_F(name, "Scene name must not be null");
  auto scene_name = std::string_view(name);
  CHECK_F(!scene_name.empty(), "Scene name must not be empty");

  return true;
}

auto RemoveScene(const char* name) -> bool
{
  CHECK_NOTNULL_F(name, "Scene name must not be null");
  auto scene_name = std::string_view(name);

  return false;
}
