//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <windows.h>

auto APIENTRY DllMain(
  HMODULE /*hModule*/, DWORD ul_reason_for_call, LPVOID /*lpReserved*/) -> BOOL
{
  switch (ul_reason_for_call) {
  case DLL_PROCESS_ATTACH:
  case DLL_THREAD_ATTACH:
  case DLL_THREAD_DETACH:
  case DLL_PROCESS_DETACH:
    break;
  default:;
  }
  return TRUE;
}
