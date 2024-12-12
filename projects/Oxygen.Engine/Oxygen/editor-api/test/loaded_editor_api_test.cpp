//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <windows.h>

#include "gtest/gtest.h"

#include "oxygen/editor-api/api.h"

// NOLINTNEXTLINE
TEST(LoadedEditorApiTest, CanUseApi) {
  using CreateGameEntityFunc =
      oxygen::ResourceHandle::HandleT (*)(OxygenGameEntityCreateInfo *);

  // Load the DLL
  const HMODULE hModule = LoadLibrary(TEXT("editor-api.dll"));
  ASSERT_NE(hModule, nullptr);

  // Get the function pointer
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
  const auto CreateGameEntity = reinterpret_cast<CreateGameEntityFunc>(
      GetProcAddress(hModule, "CreateGameEntity"));
  ASSERT_NE(CreateGameEntity, nullptr);

  // Call the function
  OxygenTransformCreateInfo transform_create_info{
      .position = {0, 0, 0},
      .rotation = {0, 0, 0},
      .scale = {1, 1, 1},
  };
  OxygenGameEntityCreateInfo entity_create_info{
      .transform = &transform_create_info,
  };
  CreateGameEntity(&entity_create_info);

  // Free the DLL
  FreeLibrary(hModule);
}
