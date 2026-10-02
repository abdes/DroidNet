//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <memory>

namespace oxygen::content::internal {
class ContentReleaseQueue;
class ContentSourceView;

struct ContentLoadScopeState final {
  std::shared_ptr<const ContentSourceView> view {};
  std::weak_ptr<const ContentReleaseQueue> epoch {};
};

} // namespace oxygen::content::internal
