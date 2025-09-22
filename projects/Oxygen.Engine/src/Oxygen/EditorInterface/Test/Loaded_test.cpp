//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <windows.h>

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/EditorInterface/Api.h>

// NOLINTNEXTLINE
TEST(LoadedEditorApiTest, CanUseApi)
{
  using CreateSceneFunc = bool (*)(const char*);
  constexpr auto dll_name = TEXT("Oxygen.Engine.EditorInterface-d.dll");

  // Load the DLL
  const HMODULE hModule = LoadLibrary(dll_name);
  ASSERT_NE(hModule, nullptr);
  // NOLINTNEXTLINE(*-pro-type-cstyle-cast)
  // Get the function pointer
  // NOLINTNEXTLINE(*-pro-type-reinterpret-cast)
  const auto CreateScene
    = reinterpret_cast<CreateSceneFunc>(GetProcAddress(hModule, "CreateScene"));
  ASSERT_NE(CreateScene, nullptr);

  // Call the function
  auto created = CreateScene("Test Scene");

  // Free the DLL
  FreeLibrary(hModule);
}
