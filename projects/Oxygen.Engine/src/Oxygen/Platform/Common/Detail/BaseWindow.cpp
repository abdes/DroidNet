//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include "./BaseWindow.h"

using oxygen::platform::BaseWindow;

BaseWindow::~BaseWindow() = default;

auto BaseWindow::RequestClose(bool force) -> void
{
    should_close_ = true;
    forced_close_ = force;
    OnCloseRequested()(force);
    forced_close_ = false;
}
