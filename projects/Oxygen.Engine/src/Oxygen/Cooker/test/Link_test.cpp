//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

// Covers: Cooked/CookedContent.cpp, Cooked/Validate.cpp

#include <cstdlib>
#include <filesystem>

#include <Oxygen/Content/LooseCookedIndex.h>
#include <Oxygen/Cooker/Cooked/CookedContent.h>
#include <Oxygen/Cooker/Cooked/CookerStorage.h>
#include <Oxygen/Cooker/Cooked/Validate.h>

auto main(int /*argc*/, char** /*argv*/) -> int
{
  namespace cooked = oxygen::content::cooked;

  using OpenFn
    = auto (*)(const cooked::CookerStorageReader&, const std::filesystem::path&)
        ->cooked::CookedContent;
  using ReadSummaryFn
    = auto (*)(const cooked::CookerStorageReader&, const std::filesystem::path&)
        ->cooked::CookedSummary;
  using ValidateFn = auto (*)(
    const cooked::CookedContent&, oxygen::content::lc::IntegrityCheck)
                       ->void;

  volatile OpenFn open = &cooked::CookedContent::Open;
  volatile ReadSummaryFn read_summary = &cooked::ReadCookedSummary;
  volatile ValidateFn validate = &cooked::Validate;

  return open == nullptr || read_summary == nullptr || validate == nullptr
    ? EXIT_FAILURE
    : EXIT_SUCCESS;
}
